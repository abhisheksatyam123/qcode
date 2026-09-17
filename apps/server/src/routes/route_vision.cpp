#include "routes_internal.h"

#include <qcode/config/config.h>
#include <qcode/core/logger.h>
#include <qcode/transform/gemini_transform.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <string>

namespace qcode {
namespace server {

namespace {


std::string get_vision_prompt(const std::string& mode) {
  const std::string base =
      "You are an expert Vision OCR and Diagram-to-Markdown system.\n"
      "Analyze the attached hand-drawn sketch, whiteboard diagram, or handwritten notes, and produce clean, accurate, high-fidelity Markdown.\n"
      "RULES:\n"
      "1. Return ONLY Markdown. Do NOT wrap the entire response in outer markdown code blocks.\n"
      "2. Convert flowcharts, architecture diagrams, sequence charts, mind maps, and topologies into valid Mermaid code blocks (```mermaid ... ```) or PlantUML if requested.\n"
      "3. Convert handwritten notes and whiteboard text into clean Markdown headers, bullet points, checklists (- [ ]), and tables.\n"
      "4. Convert mathematical equations and scientific formulas into LaTeX math ($...$ for inline, $$...$$ for display equations).\n"
      "5. Preserve node labels, connections, hierarchy, and arrow directionality accurately.";

  if (mode == "diagram") {
    return base +
           "\n\nFOCUS: DIAGRAMS & FLOWCHARTS\n"
           "- Transcribe shapes, boxes, databases, and arrows into Mermaid.js (graph TD, sequenceDiagram, classDiagram, etc.).\n"
           "- Provide concise descriptive markdown notes for the diagram components.";
  } else if (mode == "plantuml") {
    return base +
           "\n\nFOCUS: PLANTUML DIAGRAMS\n"
           "- Convert the diagram into a valid PlantUML code block (```plantuml @startuml ... @enduml ```).";
  } else if (mode == "math") {
    return base +
           "\n\nFOCUS: MATHEMATICS & FORMULAS\n"
           "- Transcribe all mathematical notation into valid LaTeX display blocks ($$...$$) and inline math ($...$).";
  } else if (mode == "notes") {
    return base +
           "\n\nFOCUS: HANDWRITTEN NOTES\n"
           "- Transcribe handwritten lists, lecture notes, agendas, and tables into structured GitHub Flavored Markdown.";
  }
  return base + "\n\nFOCUS: AUTO-DETECT (Diagrams, notes, or equations).";
}

}  // namespace

void register_vision_routes(
    httplib::Server& svr,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> /*providers_list*/) {

  svr.Post("/api/vision/ocr", [](const httplib::Request& req,
                                 httplib::Response& res) {
    nlohmann::json body;
    try {
      body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
    } catch (...) {
      res.status = 400;
      res.set_content(R"({"error":"Invalid JSON payload"})", "application/json");
      return;
    }

    std::string image_data = body.value("image", "");
    if (image_data.empty()) {
      res.status = 400;
      res.set_content(R"({"error":"Missing 'image' base64 data"})", "application/json");
      return;
    }

    std::string mode = body.value("mode", "auto");
    std::string custom_prompt = body.value("prompt", "");
    std::string prompt = custom_prompt.empty() ? get_vision_prompt(mode) : custom_prompt;

    // Strip data:image/...;base64, prefix if present
    std::string mime_type = "image/png";
    if (image_data.rfind("data:", 0) == 0) {
      auto comma_pos = image_data.find(',');
      if (comma_pos != std::string::npos) {
        auto semi_pos = image_data.find(';');
        if (semi_pos != std::string::npos && semi_pos < comma_pos) {
          mime_type = image_data.substr(5, semi_pos - 5);
        }
        image_data = image_data.substr(comma_pos + 1);
      }
    }

    // Check if Antigravity token is available
    std::string token = qcode::get_antigravity_token();

    if (!token.empty()) {
      // Direct call to Gemini via Antigravity Vertex endpoint
      nlohmann::json parts = nlohmann::json::array();
      parts.push_back({{"text", prompt}});
      parts.push_back({
          {"inlineData", {{"mimeType", mime_type}, {"data", image_data}}}
      });

      nlohmann::json gemini_req{
          {"contents", nlohmann::json::array({{{"parts", parts}}})},
          {"generationConfig", {{"temperature", 0.2}, {"maxOutputTokens", 4096}}}
      };

      std::string model = body.value("model", "gemini-2.0-flash");
      nlohmann::json envelope = qcode::gemini::wrap_antigravity_envelope(
          gemini_req, model);

      httplib::Client client("https://daily-cloudcode-pa.sandbox.googleapis.com");
      client.set_connection_timeout(10);
      client.set_read_timeout(60);

      httplib::Headers headers{
          {"Authorization", "Bearer " + token},
          {"Content-Type", "application/json"},
          {"User-Agent", "antigravity/hub/2.8.0 (aidev_client; os_type=linux; arch=amd64)"}
      };

      auto vres = client.Post("/v1internal:generateContent", headers,
                              envelope.dump(), "application/json");

      if (vres && vres->status >= 200 && vres->status < 300) {
        try {
          auto vjson = nlohmann::json::parse(vres->body);
          auto unwrapped = qcode::gemini::unwrap_envelope(vjson);
          std::string extracted_text;

          if (unwrapped.contains("candidates") && unwrapped["candidates"].is_array() &&
              !unwrapped["candidates"].empty()) {
            const auto& cand = unwrapped["candidates"][0];
            if (cand.contains("content") && cand["content"].contains("parts")) {
              for (const auto& p : cand["content"]["parts"]) {
                if (p.contains("text") && p["text"].is_string()) {
                  extracted_text += p["text"].get<std::string>();
                }
              }
            }
          }

          if (!extracted_text.empty()) {
            nlohmann::json out{
                {"markdown", extracted_text},
                {"provider", "antigravity"},
                {"model", model}
            };
            res.set_content(out.dump(2), "application/json");
            return;
          }
        } catch (const std::exception& e) {
          LOG_WARN("Antigravity response parse failed: {}", e.what());
        }
      } else {
        LOG_WARN("Antigravity call returned status {}", vres ? vres->status : -1);
      }
    }

    // Local Ollama Vision fallback if available
    try {
      httplib::Client ollama_client("http://127.0.0.1:11434");
      ollama_client.set_connection_timeout(2);
      ollama_client.set_read_timeout(60);

      nlohmann::json ollama_req{
          {"model", "qwen2.5-vl"},
          {"prompt", prompt},
          {"images", {image_data}},
          {"stream", false},
          {"options", {{"temperature", 0.2}}}
      };

      auto ores = ollama_client.Post("/api/generate", ollama_req.dump(), "application/json");
      if (ores && ores->status == 200) {
        auto ojson = nlohmann::json::parse(ores->body);
        if (ojson.contains("response") && ojson["response"].is_string()) {
          nlohmann::json out{
              {"markdown", ojson["response"].get<std::string>()},
              {"provider", "ollama"},
              {"model", "qwen2.5-vl"}
          };
          res.set_content(out.dump(2), "application/json");
          return;
        }
      }
    } catch (...) {
      // Ollama not running
    }

    // Built-in intelligent template simulation if external providers are unconfigured
    std::string fallback_markdown;
    if (mode == "diagram") {
      fallback_markdown =
          "# Converted Architecture Diagram\n\n"
          "```mermaid\n"
          "graph TD\n"
          "    UI[\"📱 Client / WebUI\"] --> Server[\"⚙️ QCode Server\"]\n"
          "    Server --> Bus[\"🚌 BusRuntime\"]\n"
          "    Server --> VLM[\"🧠 Vision Model (Gemini / Ollama)\"]\n"
          "    VLM --> Markdown[\"📝 Structured Markdown\"]\n"
          "    Markdown --> Mermaid[\"📊 Mermaid & KaTeX Preview\"]\n"
          "```\n\n"
          "### Key Topology Nodes:\n"
          "- **UI:** Infinite Canvas capturing vector strokes.\n"
          "- **Server:** `/api/vision/ocr` endpoint.\n"
          "- **Vision Model:** Synthesizes semantic arrows into valid Mermaid code.";
    } else if (mode == "math") {
      fallback_markdown =
          "# Mathematical Transcription\n\n"
          "Recognized equation:\n"
          "$$\n"
          "\\oint_C \\mathbf{B} \\cdot d\\boldsymbol{\\ell} = \\mu_0 I_{\\text{enc}} + \\mu_0 \\varepsilon_0 \\frac{d\\Phi_E}{dt}\n"
          "$$\n\n"
          "Euler-Lagrange Equation:\n"
          "$$\n"
          "\\frac{\\partial L}{\\partial q} - \\frac{d}{dt} \\left( \\frac{\\partial L}{\\partial \\dot{q}} \\right) = 0\n"
          "$$";
    } else {
      fallback_markdown =
          "# Transcribed Whiteboard Notes\n\n"
          "## Action Items & Diagram\n"
          "- [x] Integrated Infinite Canvas into Markdown viewer\n"
          "- [x] Connected Vision OCR pipeline\n"
          "- [ ] Live preview with Mermaid and KaTeX\n\n"
          "```mermaid\n"
          "flowchart LR\n"
          "    Sketch[\"✏️ Canvas Sketch\"] --> OCR[\"🔍 Vision Engine\"]\n"
          "    OCR --> Output[\"📄 Markdown + Mermaid\"]\n"
          "```";
    }

    nlohmann::json out{
        {"markdown", fallback_markdown},
        {"provider", "template-simulation"},
        {"note", "Generated via built-in simulation. Configure Antigravity token or Ollama for live VLM inference."}
    };
    res.set_content(out.dump(2), "application/json");
  });
}

}  // namespace server
}  // namespace qcode
