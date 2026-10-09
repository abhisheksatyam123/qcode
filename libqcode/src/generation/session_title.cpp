#include <qcode/session/session_title.h>

#include <qcode/config/config.h>
#include <qcode/core/logger.h>
#include <qcode/core/perf.h>
#include <qcode/generation/call_usage.h>
#include <qcode/generation/model_client.h>
#include <qcode/generation/turn_prefix.h>
#include <qcode/session/session_store.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>

namespace qcode::session_title {

namespace {

// After opencode's title agent prompt.
constexpr const char* kTitleSystem =
    "You are a title generator. You output ONLY a thread title. Nothing else.\n\n"
    "Generate a brief title that would help the user find this conversation "
    "later.\n\n"
    "Rules:\n"
    "- A single line, at most 50 characters, no explanations.\n"
    "- Use the same language as the user message.\n"
    "- Focus on the main topic or task; keep technical terms, file names and "
    "numbers exactly.\n"
    "- Drop filler words (the, this, my, a, an); never mention tools.\n"
    "- No quotes and no trailing punctuation.\n"
    "- For a greeting or a very short message, use a short descriptive title "
    "such as \"Greeting\" or \"Quick check-in\".";

constexpr size_t kMaxPromptChars = 4000;
constexpr size_t kMaxTitleChars = 60;

struct Jobs {
  std::mutex mutex;
  std::condition_variable cv;
  int running = 0;
  bool stopping = false;
  std::set<std::string> attempted;
};

// Leaked on purpose: detached jobs may outlive static destruction.
Jobs& jobs() {
  static auto* j = new Jobs();
  return *j;
}

std::string config_small_model() {
  try {
    std::ifstream in(config_path());
    if (!in) return {};
    const auto doc = nlohmann::json::parse(in, nullptr, false);
    if (doc.is_object() && doc.contains("small_model") && doc["small_model"].is_string()) {
      return doc["small_model"].get<std::string>();
    }
  } catch (...) {
  }
  return {};
}

std::string trim(std::string s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return {};
  const auto e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

}  // namespace

bool is_default_title(const std::string& title, const std::string& session_id) {
  return title.empty() || title == session_id || title.rfind("Session - ", 0) == 0;
}

std::string clean_title(std::string raw) {
  // Thinking some models inline in the text.
  for (const char* tag : {"think", "thinking"}) {
    const std::string open = std::string("<") + tag + ">";
    const std::string close = std::string("</") + tag + ">";
    size_t a;
    while ((a = raw.find(open)) != std::string::npos) {
      const size_t b = raw.find(close, a);
      raw.erase(a, b == std::string::npos ? std::string::npos : b + close.size() - a);
    }
  }
  std::string line;
  size_t pos = 0;
  while (pos <= raw.size()) {
    const size_t nl = raw.find('\n', pos);
    line = trim(raw.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
    if (!line.empty()) break;
    if (nl == std::string::npos) break;
    pos = nl + 1;
  }
  // Markdown and wrappers.
  while (!line.empty() && (line[0] == '#' || line[0] == '*' || line[0] == '-' ||
                           line[0] == '>' || line[0] == '`')) {
    line = trim(line.substr(1));
  }
  for (const char* prefix : {"Title:", "title:", "TITLE:"}) {
    if (line.rfind(prefix, 0) == 0) line = trim(line.substr(std::string(prefix).size()));
  }
  auto strip_edges = [&line](const std::string& chars) {
    while (!line.empty() && chars.find(line.front()) != std::string::npos) line.erase(0, 1);
    while (!line.empty() && chars.find(line.back()) != std::string::npos) line.pop_back();
  };
  strip_edges("\"'`*_");
  line = trim(line);
  while (!line.empty() && std::string(".:;,").find(line.back()) != std::string::npos) {
    line.pop_back();
  }
  if (line.size() > kMaxTitleChars) {
    size_t cut = line.rfind(' ', kMaxTitleChars);
    if (cut == std::string::npos || cut < kMaxTitleChars / 2) cut = kMaxTitleChars;
    // Do not split a UTF-8 sequence.
    while (cut > 0 && (static_cast<unsigned char>(line[cut]) & 0xC0) == 0x80) --cut;
    line = trim(line.substr(0, cut));
  }
  return line;
}

std::vector<std::pair<const ProviderInfo*, const ModelInfo*>> candidates(
    const std::vector<ProviderInfo>& providers, const std::string& small_model,
    const std::string& session_model) {
  std::vector<std::pair<const ProviderInfo*, const ModelInfo*>> out;
  // "provider/model" (opencode) or "provider:model"; model ids may hold '/'.
  auto add = [&](const std::string& spec) {
    const size_t sep = spec.find_first_of("/:");
    if (sep == std::string::npos) return false;
    const std::string pid = spec.substr(0, sep);
    const std::string mid = spec.substr(sep + 1);
    for (const auto& p : providers) {
      if (p.id != pid && p.name != pid) continue;
      for (const auto& m : p.models) {
        if (m.id != mid) continue;
        for (const auto& [op, om] : out) {
          if (op == &p && om == &m) return true;
        }
        out.emplace_back(&p, &m);
        return true;
      }
    }
    return false;
  };
  if (!small_model.empty() && !add(small_model)) {
    LOG_WARN("session title: small_model '{}' is not in the catalog", small_model);
  }
  if (!session_model.empty()) add(session_model);
  return out;
}

void maybe_generate_async(
    std::shared_ptr<const std::vector<ProviderInfo>> providers,
    const std::string& session_id, const std::string& first_prompt,
    std::function<void(const std::string&, const std::string&)> on_titled) {
  if (!providers || session_id.empty() || trim(first_prompt).empty()) return;
  if (session::is_child_session(session_id)) return;
  if (!is_default_title(session::get_session_title(session_id), session_id)) return;
  {
    auto& j = jobs();
    std::lock_guard<std::mutex> lock(j.mutex);
    if (j.stopping || !j.attempted.insert(session_id).second) return;
    ++j.running;
  }
  std::string prompt = first_prompt.substr(0, kMaxPromptChars);
  std::thread([providers, session_id, prompt = std::move(prompt),
               on_titled = std::move(on_titled)] {
    logger::ScopedThreadSession bind(session_id);
    std::string title;
    try {
      for (const auto& [provider, model] : candidates(*providers, config_small_model(), [&] {
             const auto [prov, model] = session::get_session_provider_model(session_id);
             return prov.empty() || model.empty() ? std::string() : prov + ":" + model;
           }())) {
        ResolvedModelClient rc = resolve_model_client(*provider, model, model->id, session_id);
        if (!rc.ok()) {
          LOG_WARN("session title: {}", rc.error);
          continue;
        }
        GenerateOptions opts;
        opts.model = rc.wire_model;
        opts.system = kTitleSystem;
        opts.messages.push_back(Message::user(
            "Generate a title for this conversation:\n<message>\n" + prompt + "\n</message>"));
        opts.max_tokens = 4096;  // room for models that think first
        opts.session_id = session_id;
        opts.max_steps = 1;
        const Model transform_model(rc.wire_model, provider->id);
        apply_turn_sampling(opts, model, transform_model);
        apply_variant_options(opts, model, "off", rc.wire_model);
        const perf::Stopwatch watch;
        GenerateResult res = rc.client.generate_text(opts);
        record_model_call(nullptr, session_id, model,
                          model_call_usage(opts, res, provider->id, model, watch.ms()), 0,
                          false, res.is_success());
        if (!res.is_success()) {
          LOG_WARN("session title: {}:{} failed: {}", provider->id, model->id,
                   res.error_message());
          continue;
        }
        title = clean_title(res.text);
        if (!title.empty()) {
          LOG_INFO("session title: {}:{} -> \"{}\"", provider->id, model->id, title);
          break;
        }
      }
      // The user may have renamed it meanwhile.
      if (!title.empty() &&
          is_default_title(session::get_session_title(session_id), session_id)) {
        session::rename_session(session_id, title);
        if (on_titled) on_titled(session_id, title);
      }
    } catch (const std::exception& e) {
      LOG_WARN("session title: {}", e.what());
    }
    auto& j = jobs();
    {
      std::lock_guard<std::mutex> lock(j.mutex);
      --j.running;
    }
    j.cv.notify_all();
  }).detach();
}

void shutdown(std::chrono::milliseconds timeout) {
  auto& j = jobs();
  std::unique_lock<std::mutex> lock(j.mutex);
  j.stopping = true;
  j.cv.wait_for(lock, timeout, [&j] { return j.running == 0; });
}

}  // namespace qcode::session_title
