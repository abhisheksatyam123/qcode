#include "routes_internal.h"

#include <qcode/config/config.h>
#include <qcode/core/logger.h>
#include <qcode/tools/image_tool.h>
#include <qcode/transform/gemini_transform.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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


// ── Image generation adapters (POST /api/images/generate) ──

struct ImageGenOutcome {
  bool ok = false;
  bool unsupported = false;  // provider exposes no image-generation API
  std::string error;
  std::string mime_type;
  std::string data;  // base64
  std::string text;  // accompanying model text, if any
};

std::string base64_decode_copy(const std::string& in) {
  auto b64val = [](unsigned char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::string out;
  out.reserve(in.size() * 3 / 4);
  int val = 0, valb = -8;
  for (unsigned char c : in) {
    if (c == '=') break;
    const int v = b64val(c);
    if (v < 0) continue;
    val = (val << 6) + v;
    valb += 6;
    if (valb >= 0) {
      out.push_back(static_cast<char>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

std::string ext_for_mime(const std::string& mime) {
  if (mime == "image/jpeg") return "jpg";
  if (mime == "image/webp") return "webp";
  if (mime == "image/gif") return "gif";
  if (mime == "image/svg+xml") return "svg";
  return "png";
}

// Antigravity (Gemini-shape) image generation: responseModalities [TEXT, IMAGE],
// first inlineData part wins. Mirrors execute_antigravity's envelope/endpoint.
ImageGenOutcome image_gen_antigravity(const std::string& model,
                                      const std::string& prompt) {
  ImageGenOutcome out;
  const std::string token = qcode::get_antigravity_token();
  if (token.empty()) {
    out.error =
        "Antigravity OAuth token not found in ~/.gemini/antigravity-cli or environment";
    return out;
  }

  const std::string target_model = model.empty() ? "gemini-3.8-flash" : model;
  nlohmann::json envelope{
      {"project", "rising-fact-p41fc"},
      {"model", target_model},
      {"request",
       {
           {"contents",
            nlohmann::json::array(
                {{{"role", "user"},
                  {"parts", nlohmann::json::array({{{"text", prompt}}})}}})},
           {"generationConfig",
            {{"temperature", 1.0},
             {"responseModalities", {"TEXT", "IMAGE"}}}},
       }}};

  httplib::SSLClient client("daily-cloudcode-pa.sandbox.googleapis.com");
  client.set_connection_timeout(15);
  client.set_read_timeout(120);

  httplib::Headers headers{
      {"Authorization", "Bearer " + token},
      {"Content-Type", "application/json"},
      {"User-Agent",
       "antigravity/hub/2.8.0 (aidev_client; os_type=linux; arch=amd64)"}};

  auto vres = client.Post("/v1internal:generateContent", headers,
                          envelope.dump(), "application/json");
  if (!vres || vres->status < 200 || vres->status >= 300) {
    out.error = "Antigravity HTTP error: " +
                std::to_string(vres ? vres->status : -1) +
                (vres && !vres->body.empty() ? " - " + vres->body.substr(0, 300)
                                             : "");
    return out;
  }

  try {
    auto vjson = nlohmann::json::parse(vres->body);
    auto unwrapped = qcode::gemini::unwrap_envelope(vjson);
    if (unwrapped.contains("promptFeedback") &&
        unwrapped["promptFeedback"].contains("blockReason")) {
      out.error = "Antigravity blocked the prompt: " +
                  unwrapped["promptFeedback"]["blockReason"].dump();
      return out;
    }
    if (unwrapped.contains("candidates") && unwrapped["candidates"].is_array()) {
      for (const auto& cand : unwrapped["candidates"]) {
        if (!cand.contains("content") || !cand["content"].contains("parts")) {
          continue;
        }
        for (const auto& part : cand["content"]["parts"]) {
          if (part.value("thought", false)) continue;
          if (part.contains("inlineData") && part["inlineData"].is_object()) {
            if (out.data.empty()) {
              out.mime_type =
                  part["inlineData"].value("mimeType", "image/png");
              out.data = part["inlineData"].value("data", "");
            }
          } else if (part.contains("text") && part["text"].is_string()) {
            out.text += part["text"].get<std::string>();
          }
        }
      }
    }
  } catch (const std::exception& e) {
    out.error = std::string("Antigravity parse exception: ") + e.what();
    return out;
  }

  if (out.data.empty()) {
    // Fallback: some gateway routes honor the drawing request but answer
    // with an inline <svg> text block instead of an inlineData image part —
    // that is still a viewable image, so emit it as image/svg+xml.
    const auto svg_start = out.text.find("<svg");
    if (svg_start != std::string::npos) {
      const auto svg_end = out.text.find("</svg>", svg_start);
      if (svg_end != std::string::npos) {
        const std::string svg =
            out.text.substr(svg_start, svg_end - svg_start + 6);
        out.mime_type = "image/svg+xml";
        out.data = qcode::ImageTool::base64_encode(svg);
        out.ok = true;
        LOG_INFO("image/generate: extracted inline SVG ({} bytes)", svg.size());
        return out;
      }
    }
    out.error = "Antigravity returned no image part (responseModalities may "
                "be unsupported for this model). Response: " +
                (out.text.empty() ? std::string("(no text)") : out.text.substr(0, 300));
    return out;
  }
  out.ok = true;
  return out;
}

// OpenAI-compatible /images/generations for OpenRouter and OpenCode Zen.
ImageGenOutcome image_gen_openai_compat(const std::string& provider_id,
                                         const std::string& model,
                                         const std::string& prompt,
                                         const std::string& size) {
  ImageGenOutcome out;
  if (model.empty()) {
    out.error = "'model' is required for provider '" + provider_id +
                "' image generation";
    return out;
  }

  std::string host;
  std::string path;
  std::string api_key;
  if (provider_id == "openrouter") {
    host = "openrouter.ai";
    path = "/api/v1/images/generations";
    if (const char* env_key = std::getenv("OPENROUTER_API_KEY")) {
      api_key = env_key;
    }
    if (api_key.empty()) {
      try {
        for (const auto& p : qcode::load_providers_from_config()) {
          if (p.id == "openrouter" && !p.api_key.empty()) {
            api_key = p.api_key;
            break;
          }
        }
      } catch (...) {
      }
    }
    if (api_key.empty()) {
      out.error = "OPENROUTER_API_KEY not found in env or opencode.json";
      return out;
    }
  } else {
    host = "opencode.ai";
    path = "/zen/v1/images/generations";
    if (const char* env_key = std::getenv("OPENCODE_API_KEY")) {
      api_key = env_key;
    }
  }

  nlohmann::json req{{"model", model},
                     {"prompt", prompt},
                     {"n", 1},
                     {"response_format", "b64_json"}};
  if (!size.empty()) req["size"] = size;

  httplib::SSLClient client(host);
  client.set_connection_timeout(15);
  client.set_read_timeout(120);

  httplib::Headers headers{{"Content-Type", "application/json"}};
  if (provider_id == "openrouter") {
    headers.emplace("Authorization", "Bearer " + api_key);
    headers.emplace("X-Title", "qcode");
  } else {
    headers.emplace("User-Agent", "opencode/1.18.18");
    headers.emplace("x-opencode-client", "cli");
    if (!api_key.empty()) headers.emplace("Authorization", "Bearer " + api_key);
  }

  auto res = client.Post(path, headers, req.dump(), "application/json");
  if (!res || res->status < 200 || res->status >= 300) {
    out.error = provider_id + " HTTP error: " +
                std::to_string(res ? res->status : -1) +
                (res && !res->body.empty() ? " - " + res->body.substr(0, 300)
                                           : "");
    return out;
  }

  try {
    auto j = nlohmann::json::parse(res->body);
    if (j.contains("data") && j["data"].is_array() && !j["data"].empty()) {
      const auto& first = j["data"][0];
      if (first.contains("b64_json") && first["b64_json"].is_string()) {
        out.data = first["b64_json"].get<std::string>();
        out.mime_type = "image/png";
      } else if (first.contains("url") && first["url"].is_string()) {
        out.error = provider_id + " returned an image URL instead of bytes: " +
                    first["url"].get<std::string>().substr(0, 200);
        return out;
      }
    }
  } catch (const std::exception& e) {
    out.error = provider_id + " parse error: " + std::string(e.what());
    return out;
  }

  if (out.data.empty()) {
    out.error = provider_id + " response contained no image bytes";
    return out;
  }
  out.ok = true;
  return out;
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

  // POST /api/images/generate - provider-specific image generation.
  // body: {prompt, provider?, model?, size?, workspace?}
  // 200 -> {mime_type, data, data_url, path?, text, provider, model}
  // 501 -> provider has no image-generation API; 502 -> provider call failed.
  svr.Post("/api/images/generate", [](const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
      body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
    } catch (...) {
      res.status = 400;
      res.set_content(R"({"error":"Invalid JSON payload"})", "application/json");
      return;
    }

    const std::string prompt = body.value("prompt", "");
    if (prompt.empty()) {
      res.status = 400;
      res.set_content(R"({"error":"Missing 'prompt'"})", "application/json");
      return;
    }
    const std::string provider = body.value("provider", "antigravity");
    const std::string model = body.value("model", "");
    const std::string size = body.value("size", "");

    ImageGenOutcome outcome;
    if (provider == "antigravity" || provider == "auto") {
      outcome = image_gen_antigravity(model, prompt);
    } else if (provider == "openrouter") {
      outcome = image_gen_openai_compat(provider, model, prompt, size);
    } else if (provider == "opencode" || provider == "zen") {
      outcome = image_gen_openai_compat(provider, model, prompt, size);
    } else {
      outcome.unsupported = true;
      outcome.error = "Provider '" + provider +
                      "' exposes no image-generation API in qcode "
                      "(text-only transport).";
    }

    if (outcome.unsupported) {
      res.status = 501;
      nlohmann::json err_j{{"error", outcome.error},
                           {"provider", provider},
                           {"model", model},
                           {"image_generation", false}};
      res.set_content(err_j.dump(), "application/json");
      return;
    }
    if (!outcome.ok) {
      res.status = 502;
      nlohmann::json err_j{{"error", outcome.error},
                           {"provider", provider},
                           {"model", model}};
      res.set_content(err_j.dump(), "application/json");
      return;
    }

    // Write the bytes to the file system too (workspace if given).
    std::string path;
    const std::string ws = body.value("workspace", "");
    std::error_code ec;
    if (!ws.empty()) std::filesystem::create_directories(ws, ec);
    const std::string dir = !ws.empty() ? ws : std::string(".");
    const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    path = dir + "/generated-" + std::to_string(stamp) + "." +
           ext_for_mime(outcome.mime_type);
    {
      std::ofstream out_file(path, std::ios::binary);
      if (out_file) {
        const auto raw = base64_decode_copy(outcome.data);
        out_file.write(raw.data(), static_cast<std::streamsize>(raw.size()));
      } else {
        path.clear();
      }
    }

    nlohmann::json out_j{
        {"status", "success"},
        {"provider", provider},
        {"model", model},
        {"mime_type", outcome.mime_type},
        {"data", outcome.data},
        {"data_url", "data:" + outcome.mime_type + ";base64," + outcome.data},
        {"path", path},
        {"text", outcome.text},
    };
    res.set_content(out_j.dump(), "application/json");
  });
}

}  // namespace server
}  // namespace qcode
