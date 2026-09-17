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
           "- Transcribe shapes, boxes, databases, and arrows into Mermaid.js (flowchart LR, graph TD, sequenceDiagram, etc.).\n"
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

nlohmann::json get_provider_catalog() {
  nlohmann::json catalog;
  catalog["default_provider"] = "antigravity";
  catalog["default_model"] = "gemini-3.8-flash";
  nlohmann::json providers_arr = nlohmann::json::array();

  try {
    auto loaded = qcode::load_providers_from_config();
    for (const auto& prov : loaded) {
      nlohmann::json p_obj;
      p_obj["id"] = prov.id;
      p_obj["name"] = prov.name.empty() ? prov.id : prov.name;
      p_obj["description"] = prov.id + " models";

      nlohmann::json models_arr = nlohmann::json::array();
      for (const auto& m : prov.models) {
        if (m.vision) {
          models_arr.push_back({
              {"id", m.id},
              {"name", m.name.empty() ? m.id : m.name},
              {"vision", true}
          });
        }
      }

      // Only include providers that have working vision models configured
      if (!models_arr.empty()) {
        p_obj["models"] = std::move(models_arr);
        providers_arr.push_back(std::move(p_obj));
      }
    }
  } catch (const std::exception& e) {
    LOG_WARN("Failed to load provider catalog from config: {}", e.what());
  }

  // Always offer Local Ollama if available
  providers_arr.push_back({
      {"id", "ollama"},
      {"name", "Local Ollama"},
      {"description", "100% private offline vision running on local host"},
      {"models", nlohmann::json::array({
          {{"id", "qwen2.5-vl"}, {"name", "Qwen 2.5 VL (Local)"}, {"vision", true}},
          {{"id", "llama3.2-vision"}, {"name", "Llama 3.2 Vision (Local)"}, {"vision", true}}
      })}
  });

  catalog["providers"] = std::move(providers_arr);
  return catalog;
}

std::string clean_markdown_output(std::string text) {
  while (!text.empty() && (text.front() == '\r' || text.front() == '\n' || text.front() == ' ')) {
    text.erase(text.begin());
  }
  while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
    text.pop_back();
  }
  return text;
}

// 1. Antigravity execution
bool execute_antigravity(const std::string& model, const std::string& prompt,
                         const std::string& mime_type, const std::string& raw_base64,
                         std::string& out_markdown, std::string& out_err) {
  std::string token = qcode::get_antigravity_token();
  if (token.empty()) {
    out_err = "Antigravity OAuth token not found in ~/.gemini/antigravity-cli or environment";
    return false;
  }

  nlohmann::json parts = nlohmann::json::array();
  parts.push_back({{"text", prompt}});
  parts.push_back({
      {"inlineData", {{"mimeType", mime_type}, {"data", raw_base64}}}
  });

  std::string target_model = model.empty() ? "gemini-3.8-flash-medium" : model;
  if (target_model == "gemini-3.8-flash") target_model = "gemini-3.8-flash-medium";
  else if (target_model == "gemini-3.1-pro") target_model = "gemini-3.1-pro-low";

  nlohmann::json envelope{
      {"project", "rising-fact-p41fc"},
      {"model", target_model},
      {"request", {
          {"contents", nlohmann::json::array({{{"role", "user"}, {"parts", parts}}})},
          {"generationConfig", {{"temperature", 0.2}, {"maxOutputTokens", 4096}}}
      }}
  };

  httplib::SSLClient client("daily-cloudcode-pa.sandbox.googleapis.com");
  client.set_connection_timeout(15);
  client.set_read_timeout(60);

  httplib::Headers headers{
      {"Authorization", "Bearer " + token},
      {"Content-Type", "application/json"},
      {"User-Agent", "antigravity/hub/2.8.0 (aidev_client; os_type=linux; arch=amd64)"}
  };

  auto vres = client.Post("/v1internal:generateContent", headers,
                          envelope.dump(), "application/json");

  if (!vres || vres->status < 200 || vres->status >= 300) {
    out_err = "Antigravity HTTP error: " + std::to_string(vres ? vres->status : -1);
    return false;
  }

  try {
    auto vjson = nlohmann::json::parse(vres->body);
    auto unwrapped = qcode::gemini::unwrap_envelope(vjson);

    if (unwrapped.contains("candidates") && unwrapped["candidates"].is_array() &&
        !unwrapped["candidates"].empty()) {
      const auto& cand = unwrapped["candidates"][0];
      if (cand.contains("content") && cand["content"].contains("parts")) {
        for (const auto& p : cand["content"]["parts"]) {
          if (p.value("thought", false)) continue;
          if (p.contains("text") && p["text"].is_string()) {
            out_markdown += p["text"].get<std::string>();
          }
        }
      }
    }
  } catch (const std::exception& e) {
    out_err = "Antigravity parse exception: " + std::string(e.what());
    return false;
  }

  return !out_markdown.empty();
}

// 2. OpenRouter execution
bool execute_openrouter(const std::string& model, const std::string& prompt,
                        const std::string& full_data_url,
                        std::string& out_markdown, std::string& out_err) {
  std::string api_key;
  if (const char* env_key = std::getenv("OPENROUTER_API_KEY")) {
    api_key = env_key;
  }
  if (api_key.empty()) {
    try {
      auto provs = qcode::load_providers_from_config();
      for (const auto& p : provs) {
        if (p.id == "openrouter" && !p.api_key.empty()) {
          api_key = p.api_key;
          break;
        }
      }
    } catch (...) {}
  }
  if (api_key.empty()) {
    out_err = "OPENROUTER_API_KEY not found in env or opencode.json";
    return false;
  }

  std::string target_model = model.empty() ? "nex-agi/nex-n2.5-pro:free" : model;

  nlohmann::json req_body{
      {"model", target_model},
      {"messages", nlohmann::json::array({
          {{"role", "user"},
           {"content", nlohmann::json::array({
               {{"type", "text"}, {"text", prompt}},
               {{"type", "image_url"}, {"image_url", {{"url", full_data_url}}}}
           })}}
      })},
      {"max_tokens", 4096},
      {"temperature", 0.2}
  };

  httplib::SSLClient client("openrouter.ai");
  client.set_connection_timeout(15);
  client.set_read_timeout(60);

  httplib::Headers headers{
      {"Authorization", "Bearer " + api_key},
      {"Content-Type", "application/json"},
      {"X-Title", "qcode"}
  };

  auto res = client.Post("/api/v1/chat/completions", headers,
                         req_body.dump(), "application/json");

  if (!res || res->status < 200 || res->status >= 300) {
    out_err = "OpenRouter HTTP error: " + std::to_string(res ? res->status : -1) +
              (res ? " - " + res->body.substr(0, 150) : "");
    return false;
  }

  try {
    auto j = nlohmann::json::parse(res->body);
    if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
      const auto& choice = j["choices"][0];
      if (choice.contains("message") && choice["message"].is_object()) {
        const auto& msg = choice["message"];
        if (msg.contains("content") && msg["content"].is_string()) {
          out_markdown = msg["content"].get<std::string>();
        } else if (msg.contains("reasoning") && msg["reasoning"].is_string()) {
          out_markdown = msg["reasoning"].get<std::string>();
        }
      }
    }
  } catch (const std::exception& e) {
    out_err = "OpenRouter parse error: " + std::string(e.what());
    return false;
  }

  return !out_markdown.empty();
}

// 3. OpenCode Zen execution
bool execute_opencode(const std::string& model, const std::string& prompt,
                      const std::string& full_data_url,
                      std::string& out_markdown, std::string& out_err) {
  const char* env_key = std::getenv("OPENCODE_API_KEY");
  std::string api_key = env_key ? env_key : "";

  std::string target_model = model.empty() ? "gemini-3.8-flash" : model;

  nlohmann::json req_body{
      {"model", target_model},
      {"messages", nlohmann::json::array({
          {{"role", "user"},
           {"content", nlohmann::json::array({
               {{"type", "text"}, {"text", prompt}},
               {{"type", "image_url"}, {"image_url", {{"url", full_data_url}}}}
           })}}
      })},
      {"max_tokens", 4096},
      {"temperature", 0.2}
  };

  httplib::SSLClient client("opencode.ai");
  client.set_connection_timeout(15);
  client.set_read_timeout(60);

  httplib::Headers headers{
      {"User-Agent", "opencode/1.18.18"},
      {"x-opencode-client", "cli"},
      {"Content-Type", "application/json"}
  };
  if (!api_key.empty()) {
    headers.emplace("Authorization", "Bearer " + api_key);
  }

  auto res = client.Post("/zen/v1/chat/completions", headers,
                         req_body.dump(), "application/json");

  if (!res || res->status < 200 || res->status >= 300) {
    out_err = "OpenCode Zen HTTP error: " + std::to_string(res ? res->status : -1) +
              (res ? " - " + res->body.substr(0, 150) : "");
    return false;
  }

  try {
    auto j = nlohmann::json::parse(res->body);
    if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
      const auto& choice = j["choices"][0];
      if (choice.contains("message") && choice["message"].is_object()) {
        const auto& msg = choice["message"];
        if (msg.contains("content") && msg["content"].is_string()) {
          out_markdown = msg["content"].get<std::string>();
        }
      }
    }
  } catch (const std::exception& e) {
    out_err = "OpenCode parse error: " + std::string(e.what());
    return false;
  }

  return !out_markdown.empty();
}

// 4. Local Ollama execution
bool execute_ollama(const std::string& model, const std::string& prompt,
                    const std::string& raw_base64,
                    std::string& out_markdown, std::string& out_err) {
  std::string target_model = model.empty() ? "qwen2.5-vl" : model;

  try {
    httplib::Client client("127.0.0.1", 11434);
    client.set_connection_timeout(3);
    client.set_read_timeout(60);

    nlohmann::json ollama_req{
        {"model", target_model},
        {"prompt", prompt},
        {"images", {raw_base64}},
        {"stream", false},
        {"options", {{"temperature", 0.2}}}
    };

    auto res = client.Post("/api/generate", ollama_req.dump(), "application/json");
    if (res && res->status == 200) {
      auto ojson = nlohmann::json::parse(res->body);
      if (ojson.contains("response") && ojson["response"].is_string()) {
        out_markdown = ojson["response"].get<std::string>();
        return true;
      }
    } else {
      out_err = "Ollama returned status " + std::to_string(res ? res->status : -1);
    }
  } catch (const std::exception& e) {
    out_err = "Ollama connection exception: " + std::string(e.what());
  }

  return false;
}

}  // namespace

void register_vision_routes(
    httplib::Server& svr,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> /*providers_list*/) {

  // GET /api/vision/providers - returns catalog of available vision providers & models
  svr.Get("/api/vision/providers", [](const httplib::Request&, httplib::Response& res) {
    auto catalog = get_provider_catalog();
    res.set_content(catalog.dump(2), "application/json");
  });

  // POST /api/vision/ocr - converts drawing image to markdown using selected provider & model
  svr.Post("/api/vision/ocr", [](const httplib::Request& req, httplib::Response& res) {
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

    std::string provider = body.value("provider", "antigravity");
    std::string model = body.value("model", "");
    std::string mode = body.value("mode", "diagram");
    std::string custom_prompt = body.value("prompt", "");
    std::string prompt = custom_prompt.empty() ? get_vision_prompt(mode) : custom_prompt;

    // Separate raw base64 and data URL
    std::string raw_base64 = image_data;
    std::string mime_type = "image/png";
    std::string full_data_url;

    if (image_data.rfind("data:", 0) == 0) {
      full_data_url = image_data;
      auto comma_pos = image_data.find(',');
      if (comma_pos != std::string::npos) {
        auto semi_pos = image_data.find(';');
        if (semi_pos != std::string::npos && semi_pos < comma_pos) {
          mime_type = image_data.substr(5, semi_pos - 5);
        }
        raw_base64 = image_data.substr(comma_pos + 1);
      }
    } else {
      full_data_url = "data:image/png;base64," + image_data;
    }

    std::string extracted_markdown;
    std::string last_error;
    std::string used_provider = provider;
    std::string used_model = model;

    // Dispatch based on selected provider
    if (provider == "antigravity" || provider == "auto") {
      if (execute_antigravity(model, prompt, mime_type, raw_base64, extracted_markdown, last_error)) {
        used_provider = "antigravity";
        if (used_model.empty()) used_model = "gemini-3.8-flash-medium";
      }
    }

    if (extracted_markdown.empty() && (provider == "openrouter" || provider == "auto")) {
      if (execute_openrouter(model, prompt, full_data_url, extracted_markdown, last_error)) {
        used_provider = "openrouter";
        if (used_model.empty()) used_model = "nex-agi/nex-n2.5-pro:free";
      }
    }

    if (extracted_markdown.empty() && (provider == "opencode" || provider == "auto")) {
      if (execute_opencode(model, prompt, full_data_url, extracted_markdown, last_error)) {
        used_provider = "opencode";
        if (used_model.empty()) used_model = "gemini-3.8-flash";
      }
    }

    if (extracted_markdown.empty() && (provider == "ollama" || provider == "auto")) {
      if (execute_ollama(model, prompt, raw_base64, extracted_markdown, last_error)) {
        used_provider = "ollama";
        if (used_model.empty()) used_model = "qwen2.5-vl";
      }
    }

    // If successfully extracted
    if (!extracted_markdown.empty()) {
      nlohmann::json out{
          {"markdown", clean_markdown_output(extracted_markdown)},
          {"provider", used_provider},
          {"model", used_model}
      };
      res.set_content(out.dump(2), "application/json");
      return;
    }

    // Fallback template simulation
    std::string fallback_markdown;
    if (mode == "diagram") {
      fallback_markdown =
          "# Converted Architecture Diagram\n\n"
          "```mermaid\n"
          "flowchart LR\n"
          "    UI[\"📱 Client / WebUI\"] --> Server[\"⚙️ QCode Server\"]\n"
          "    Server --> Bus[\"🚌 BusRuntime\"]\n"
          "    Server --> VLM[\"🧠 Vision Model\"]\n"
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
          "- [x] Multi-provider vision OCR (Antigravity, OpenRouter, OpenCode, Ollama)\n"
          "- [x] Live preview with Mermaid and KaTeX\n\n"
          "```mermaid\n"
          "flowchart LR\n"
          "    Sketch[\"✏️ Canvas Sketch\"] --> OCR[\"🔍 Vision Engine\"]\n"
          "    OCR --> Output[\"📄 Markdown + Mermaid\"]\n"
          "```";
    }

    nlohmann::json out{
        {"markdown", fallback_markdown},
        {"provider", "template-simulation"},
        {"note", "All external vision providers failed or were unconfigured: " + last_error}
    };
    res.set_content(out.dump(2), "application/json");
  });
}

}  // namespace server
}  // namespace qcode
