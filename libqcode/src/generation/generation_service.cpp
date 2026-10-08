#include <qcode/generation/turn_prefix.h>
#include <qcode/generation/generation_service.h>
#include <qcode/session/task_notes.h>
#include <qcode/core/perf.h>
#include <qcode/generation/generation_continue.h>
#include <qcode/config/config.h>
#include <qcode/session/token_budget.h>
#include <qcode/tools/tool_catalog.h>
#include <qcode/tools/subagent_router.h>
#include <qcode/tools/task_target.h>
#include <qcode/tools/tool_executor.h>
#include <qcode/tools/multi_step_coordinator.h>
#include <qcode/session/session_store.h>
#include <qcode/session/subagent_stats.h>
#include <qcode/core/event.h>
#include <qcode/core/errors.h>
#include "generation/stream_step.h"

#include <algorithm>
#include <atomic>
#include <random>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <qcode/core/client.h>
#include <qcode/core/logger.h>
#include <qcode/providers/openai.h>
#include <qcode/providers/provider_profile.h>
#include <qcode/transform/provider_transform.h>
#include <qcode/providers/registry.h>
#include <qcode/providers/authenticated_providers.h>
#include <nlohmann/json.hpp>

namespace qcode {

using namespace contract;

static bool looks_like_auth_error(const std::string& message) {
  // Zen returns ModelError / content-policy failures as HTTP 401. Those are
  // request-shape problems, not expired credentials.
  if (message.find("ModelError") != std::string::npos ||
      message.find("is not supported") != std::string::npos ||
      message.find("content_filter") != std::string::npos) {
    return false;
  }
  return message.find("401") != std::string::npos ||
         message.find("authentication") != std::string::npos ||
         message.find("unauthenticated") != std::string::npos ||
         message.find("invalid_grant") != std::string::npos ||
         message.find("access token") != std::string::npos;
}

// Fingerprint of one tool step: the calls AND their results, so "same call,
// new output" is not treated as a stuck loop (e.g. re-read after edit,
// poll/retry bash). Hashes the JSON in place instead of dumping every tool
// output to a string (and keeping it) each step.
static size_t tool_step_fingerprint(const std::vector<qcode::ToolCall>& calls,
                                    const std::vector<qcode::ToolResult>& results) {
  size_t h = 0;
  const auto mix = [&h](size_t v) {
    h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
  };
  const std::hash<std::string> str_hash;
  const std::hash<nlohmann::json> json_hash;
  for (const auto& call : calls) {
    mix(str_hash(call.tool_name));
    mix(json_hash(call.arguments));
  }
  mix(calls.size());
  for (const auto& res : results) {
    mix(str_hash(res.tool_name));
    mix(res.is_success() ? 1 : 2);
    mix(json_hash(res.result));
    if (res.error) mix(str_hash(*res.error));
  }
  return h;
}

// ──────────────────────────────────────────────────────────────
//  Multi-agent: nested subagent turn (routed model with fallback)
// ──────────────────────────────────────────────────────────────
namespace {
// Models tried per task: the one the lead named (if any), then free models
// ranked by the subagent router (qcode/tools/subagent_router.h).
constexpr int kSubagentMaxAttempts = 4;

// The lead named a provider/model (not inherit/parent/default).
bool subagent_model_named(const nlohmann::json& args) {
  const auto named = [](const nlohmann::json& v) {
    return v.is_string() && !v.get<std::string>().empty() &&
           !is_inherit_model_id(v.get<std::string>());
  };
  if ((args.contains("provider") && named(args["provider"])) ||
      (args.contains("model") && named(args["model"]))) {
    return true;
  }
  return args.contains("models") && args["models"].is_array() &&
         std::any_of(args["models"].begin(), args["models"].end(), named);
}

// An account-wide limit (OpenRouter's free-models-per-day, exhausted credits)
// covers every model of the provider, so the rest of this task's chain skips
// it. Plain 429s are per model (antigravity meters each model family on its
// own): only that model rests, the provider's other models are still tried.
bool provider_wide_limit(std::string msg) {
  std::transform(msg.begin(), msg.end(), msg.begin(), ::tolower);
  if (msg.find("upstream") != std::string::npos) return false;
  for (const char* s : {"per-day", "per day", "daily", "credits", "insufficient balance"}) {
    if (msg.find(s) != std::string::npos) return true;
  }
  return false;
}

bool subagent_wants_cursor(const nlohmann::json& args) {
  auto has = [](const std::string& v) {
    std::string l = v;
    std::transform(l.begin(), l.end(), l.begin(), ::tolower);
    return l.find("cursor") != std::string::npos;
  };
  if (has(args.value("provider", "")) || has(args.value("model", ""))) return true;
  if (args.contains("models") && args["models"].is_array())
    for (const auto& m : args["models"])
      if (m.is_string() && has(m.get<std::string>())) return true;
  return false;
}
}  // namespace

// Applies a model's opencode.json reasoning config to `opts`: the thinking
// wire form ("thinking": {type, display}) and the resolved variant
// ("variants": {"<id>": {effort, max_tokens, budget_tokens, prompt}}).
// `requested` is the session /variant ("" = model default; "off" disables
// thinking where the model allows it). Without catalog info the requested
// id passes through as the effort.
static void apply_variant_options(qcode::GenerateOptions& opts,
                                  const ModelInfo* model,
                                  const std::string& requested,
                                  const std::string& model_id) {
  std::string variant_id;
  if (model) {
    if (!requested.empty() || model->reasoning) {
      variant_id = ProviderTransform::resolve_session_variant(*model, requested);
    }
    if (!model->thinking_type.empty()) opts.thinking_type = model->thinking_type;
    if (!model->thinking_display.empty()) opts.thinking_display = model->thinking_display;
  } else if (requested != "off") {
    variant_id = requested;
  }
  if (variant_id.empty() || variant_id == "off") return;

  const VariantInfo* variant =
      model ? ProviderTransform::find_variant(*model, variant_id) : nullptr;
  opts.reasoning_effort =
      model ? ProviderTransform::variant_wire_effort(*model, variant_id) : variant_id;
  opts.reasoning_variant = variant_id;
  if (variant != nullptr) {
    if (variant->budget_tokens > 0) opts.budget_tokens = variant->budget_tokens;
    if (variant->max_tokens > 0) {
      // limit.output is the hard cap; the variant only raises the request.
      const int cap = model->output_limit > 0 ? model->output_limit : variant->max_tokens;
      opts.max_tokens =
          std::max(opts.max_tokens.value_or(0), std::min(variant->max_tokens, cap));
    }
    if (!variant->prompt.empty()) {
      opts.system += "\n\n";
      opts.system += variant->prompt;
    }
  }
  if (!requested.empty() && variant_id != requested) {
    LOG_INFO("generation_service: clamped variant '{}' -> '{}' for {}", requested,
             variant_id, model_id);
  } else if (requested.empty()) {
    LOG_INFO("generation_service: default thinking variant '{}' for {}", variant_id,
             model_id);
  }
  LOG_INFO("generation_service: variant={} effort={} max_tokens={} budget_tokens={} "
           "prompt={} thinking={} model={}",
           variant_id, opts.reasoning_effort.value_or(""), opts.max_tokens.value_or(0),
           opts.budget_tokens.value_or(0), variant != nullptr && !variant->prompt.empty(),
           opts.thinking_type.value_or("default"), model_id);
}

static JsonValue run_subagent_turn_multi(
    std::shared_ptr<std::vector<ProviderInfo>> providers,
    const std::string& default_provider_id,
    const std::string& default_model_id,
    const std::string& workspace,
    std::shared_ptr<std::atomic<bool>> main_abort_flag,
    std::shared_ptr<std::atomic<bool>> task_abort_flag,
    const JsonValue& args) {
  std::string prompt_text = args.value("prompt", args.value("task", args.value("objective", "")));
  if (prompt_text.empty()) {
    prompt_text = args.value("description", "");
  }
  if (prompt_text.empty()) {
    return JsonValue{{"error", "task prompt is required"}};
  }

  const bool is_background = args.value("background", args.value("run_in_background", false));
  auto aborted = [&]() {
    if (task_abort_flag && task_abort_flag->load()) return true;
    if (!is_background && main_abort_flag && main_abort_flag->load()) return true;
    return false;
  };
  if (aborted()) {
    return JsonValue{{"error", "Subagent cancelled due to abort flag"}};
  }

  std::shared_ptr<std::atomic<bool>> effective_abort;
  if (is_background) {
    effective_abort = task_abort_flag ? task_abort_flag : std::make_shared<std::atomic<bool>>(false);
  } else {
    effective_abort = main_abort_flag ? main_abort_flag : (task_abort_flag ? task_abort_flag : std::make_shared<std::atomic<bool>>(false));
  }

  std::string subagent_type = args.value("subagent_type", "general");
  std::string mode = args.value("mode", "explore");
  std::string description = args.value("description", "");
  std::string objective = args.value("objective", "");
  const routing::Difficulty difficulty =
      routing::parse_difficulty(args.value("difficulty", "medium"));
  const std::string parent_session_id = args.value("parent_session_id", "");
  const std::string sub_session_id =
      args.value("sessionId", args.value("session_id", args.value("task_id", "")));

  if (!providers) {
    return JsonValue{{"error", "No available AI provider configured for subagent"}};
  }

  // Chain: the model the lead named (paid allowed), then free models ranked
  // by learned success, difficulty, latency and load.
  struct Candidate {
    const ProviderInfo* provider;
    const ModelInfo* model;  // null for a named model missing from the catalog
    std::string model_id;
    std::string reason;
    std::optional<double> expected;
  };
  std::vector<Candidate> chain;
  if (subagent_model_named(args)) {
    // Resolved without the lead's ids: naming the lead's own model falls
    // through to the router instead of a fixed alternate.
    const SubagentTarget t = resolve_subagent_target(args, *providers, "", "");
    if (!t.provider) {
      return JsonValue{{"error", t.error.empty() ? "No available AI provider configured for subagent"
                                                 : t.error}};
    }
    if (!matches_orchestrator(t.provider_id, t.model_id, default_provider_id, default_model_id)) {
      chain.push_back({t.provider, t.model_info, t.model_id, "explicit", std::nullopt});
    }
  }
  routing::RouteRequest req;
  req.mode = mode;
  req.difficulty = difficulty;
  req.lead_provider = default_provider_id;
  req.lead_model = default_model_id;
  req.allow_cursor = subagent_wants_cursor(args);
  req.now = std::time(nullptr);
  req.inflight_by_provider = routing::inflight_snapshot();
  static thread_local std::mt19937 rng{std::random_device{}()};
  for (const auto& r : routing::rank_targets(*providers, session::load_subagent_stats(), req, rng)) {
    if (!chain.empty() && r.provider == chain.front().provider &&
        r.model->id == chain.front().model_id) {
      continue;
    }
    chain.push_back({r.provider, r.model, r.model->id, r.reason, r.expected});
  }
  if (chain.empty()) {
    return JsonValue{{"error", "No free working model for subagents; name one with "
                               "model: \"provider:model\"."}};
  }

  JsonValue out;
  try {
  std::string last_error;
  std::set<std::string> limited_providers;  // hit a provider-wide 429/quota
  int attempt = 0;
  for (const auto& cand : chain) {
    if (attempt >= kSubagentMaxAttempts) break;
    if (limited_providers.contains(cand.provider->id)) continue;
    ++attempt;
    const ProviderInfo* target_provider = cand.provider;
    const ModelInfo* target_model_info = cand.model;
    const std::string& target_model_id = cand.model_id;
    if (attempt > 1) {
      LOG_WARN("subagent attempt {}/{}: {}:{} ({}; prev: {})", attempt, kSubagentMaxAttempts,
               target_provider->id, target_model_id, cand.reason, last_error);
    }

    routing::ScopedInflight inflight(target_provider->id);
    const auto started = std::chrono::steady_clock::now();
    const int64_t started_at = std::time(nullptr);
    const auto record = [&](routing::Outcome outcome, const std::string& error) {
      session::SubagentRun run;
      run.task_id = sub_session_id;
      run.attempt = attempt;
      run.parent_session_id = parent_session_id;
      run.provider = target_provider->id;
      run.model = target_model_id;
      run.mode = mode;
      run.difficulty = std::string(routing::to_string(difficulty));
      run.outcome = outcome;
      run.error = error;
      run.latency_ms = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - started).count();
      run.started_at = started_at;
      session::record_subagent_run(run);
    };

    qcode::providers::ProviderOptions prov_opts;
    prov_opts.base_url = target_provider->api_url;
    prov_opts.api_key = target_provider->api_key;
    prov_opts.headers = target_provider->headers;
    prov_opts.protocol = (target_model_info && !target_model_info->protocol.empty())
                             ? target_model_info->protocol
                             : target_provider->protocol;
    prov_opts.project_id = target_provider->project_id;

    qcode::providers::register_authenticated_providers();
    if (target_provider->id == "cursor") {
      if (prov_opts.api_key.empty()) {
        prov_opts.api_key = get_cursor_access_token();
      }
    } else if (target_provider->id.find("antigravity") != std::string::npos ||
               target_provider->name.find("Antigravity") != std::string::npos) {
      const auto fresh = get_antigravity_token(/*force_refresh=*/false);
      if (!fresh.empty()) {
        prov_opts.api_key = fresh;
      }
    } else if (target_provider->id == "openrouter") {
      if (prov_opts.api_key.empty()) {
        const char* key = std::getenv("OPENROUTER_API_KEY");
        if (key && *key) prov_opts.api_key = key;
      }
    } else if (target_provider->id == "anthropic" ||
               target_provider->name.find("Anthropic") != std::string::npos) {
      if (prov_opts.api_key.empty() || prov_opts.api_key.starts_with("sk-ant-oat")) {
        const auto fresh = get_anthropic_token(/*force_refresh=*/false);
        if (!fresh.empty()) prov_opts.api_key = fresh;
      }
    } else if (target_provider->id == "openai" ||
               target_provider->name.find("OpenAI") != std::string::npos) {
      if (prov_opts.api_key.empty()) {
        const char* key = std::getenv("OPENAI_API_KEY");
        if (key && *key) prov_opts.api_key = key;
      }
    }

    const auto call = prepare_provider_call(prov_opts, target_provider->id, target_model_id, "");
    std::string wire_model = call.wire_model_id.empty() ? target_model_id : call.wire_model_id;

    auto resolution = qcode::providers::ProviderRegistry::instance().resolve(
        target_provider->id, prov_opts);
    if (!resolution.ok()) {
      last_error = "Failed to resolve subagent client for provider '" +
                   target_provider->id + "': " + resolution.error;
      LOG_WARN("subagent fallback: {}", last_error);
      record(routing::Outcome::kTransientError, last_error);  // setup, not model quality
      continue;
    }
    qcode::Client subagent_client = std::move(resolution.client);

    // Route this thread's logs to the child's own session file for the
    // duration of the subagent turn (restored when the scope exits).
    std::optional<qcode::logger::ScopedThreadSession> child_bind;
    if (!sub_session_id.empty()) child_bind.emplace(sub_session_id);
    if (!sub_session_id.empty() && target_provider) {
      qcode::session::set_session_provider_model(
          sub_session_id, target_provider->name, target_model_id);
    }

    std::ostringstream sub_sys;
    sub_sys << "You are an autonomous '" << subagent_type
            << "' subagent delegated by the Lead Orchestrator.\n";
    if (!description.empty()) {
      sub_sys << "Task: " << description << "\n";
    }
    if (!objective.empty()) {
      sub_sys << "Objective: " << objective << "\n";
    }
    if (!mode.empty()) {
      sub_sys << "Delegation Mode: " << mode << "\n";
    }
    if (args.contains("scope") && args["scope"].is_array() && !args["scope"].empty()) {
      sub_sys << "Scope:\n";
      for (const auto& s : args["scope"]) {
        if (s.is_string()) sub_sys << "- " << s.get<std::string>() << "\n";
      }
    }
    if (args.contains("out_of_scope") && args["out_of_scope"].is_array() && !args["out_of_scope"].empty()) {
      sub_sys << "Out of Scope:\n";
      for (const auto& s : args["out_of_scope"]) {
        if (s.is_string()) sub_sys << "- " << s.get<std::string>() << "\n";
      }
    }
    if (mode == "implement" && args.contains("allowed_paths") && args["allowed_paths"].is_array()) {
      sub_sys << "Allowed Paths for edits:\n";
      for (const auto& p : args["allowed_paths"]) {
        if (p.is_string()) sub_sys << "- " << p.get<std::string>() << "\n";
      }
    }

    sub_sys << "\nOperational Directives:\n";
    if (mode == "explore") {
      sub_sys << "- Mode is EXPLORE: Read-only inspection and analysis. STRICTLY FORBIDDEN to modify any files.\n";
    } else if (mode == "implement") {
      sub_sys << "- Mode is IMPLEMENT: Execute focused, high-precision edits strictly within the allowed paths.\n";
    } else if (mode == "verify") {
      sub_sys << "- Mode is VERIFY: Run tests, build checks, linters, or audits to verify correctness.\n";
    } else {
      sub_sys << "- Work independently and execute the required operations.\n";
    }
    sub_sys << "- Do not prompt or query the user. Use the bash tool to inspect files and execute commands.\n";
    {
      const auto notes = qcode::task_notes::resolve(workspace);
      if (notes.exists) {
        sub_sys << "- Task file: `" << notes.path
                << "` (sections: Tasks, Systems, Log). Read it first for context. "
                   "Do NOT rewrite it; you may only append one-line entries to its "
                   "Log section with a single `>>` append, formatted "
                   "`- YYYY-MM-DD HH:MM [sub:" << mode << "] <event>`.\n";
      }
    }
    sub_sys << "- Conclude with a clear, concise, structured summary detailing findings, changes, or test outcomes.\n\n";
    const bool subagent_vision = (target_model_info != nullptr && target_model_info->vision);
    const auto subagent_tool_cfg = ToolConfig::subagent(subagent_vision);
    sub_sys << ToolCatalog::build_tool_section(subagent_tool_cfg);



    int max_steps = 0;
    if (const char* env_steps = std::getenv("QCODE_MAX_STEPS")) {
      try {
        const int v = std::stoi(env_steps);
        if (v >= 0) max_steps = v;
      } catch (...) {}
    }
    qcode::GenerateOptions sub_opts(wire_model, sub_sys.str(), "");
    // Subagent turns must use the same bounded output budget as the parent.
    // Leaving this unset lets OpenRouter apply a model-specific default (for
    // some models, 131072), which can turn an otherwise valid delegation into
    // HTTP 402 when the account cannot afford that completion budget.
    if (target_model_info && target_model_info->output_limit > 0) {
      sub_opts.max_tokens =
          ProviderTransform::max_output_tokens(target_model_info->output_limit);
    } else {
      sub_opts.max_tokens = 8192;
    }
    // Subagents think at their model's configured default variant.
    apply_variant_options(sub_opts, target_model_info, "", target_model_id);
    sub_opts.tools = ToolCatalog::build_definitions(subagent_tool_cfg);
    sub_opts.max_steps = max_steps;
    sub_opts.workspace = workspace;
    sub_opts.abort_flag = effective_abort;
    sub_opts.session_id = sub_session_id;
    bool can_edit = args.value("can_edit", false);
    if (mode == "explore") can_edit = false;
    if (mode == "implement") can_edit = true;
    sub_opts.can_edit = can_edit;
    sub_opts.messages.push_back(Message::user(prompt_text));

    if (!sub_session_id.empty()) {
      sub_opts.on_tool_call_start = [sub_session_id](const ToolCall& call) {
        nlohmann::json call_json = {
            {"id", call.id},
            {"name", call.tool_name},
            {"arguments", call.arguments},
        };
        qcode::session::save_message(sub_session_id, "ToolCall", call_json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
      };
      sub_opts.on_tool_call_finish = [sub_session_id](const ToolResult& res) {
        nlohmann::json result_json = {
            {"tool_call_id", res.tool_call_id},
            {"tool_name", res.tool_name},
            {"result", res.result},
            {"is_error", !res.is_success()},
            {"duration_ms", 0},
        };
        qcode::session::save_message(sub_session_id, "ToolResult", result_json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
      };
    }

    qcode::GenerateResult res = MultiStepCoordinator::execute_multi_step(
        sub_opts, [&subagent_client](const GenerateOptions& step_opts) {
          return subagent_client.generate_text(step_opts);
        });

    std::string error;
    if (!res.is_success()) {
      error = !res.error_message().empty() ? res.error_message() : "subagent generation failed";
    }
    routing::Outcome outcome = aborted() ? routing::Outcome::kAborted
                                         : routing::classify_outcome(res.is_success(), error);
    if (outcome == routing::Outcome::kSuccess &&
        res.text.find_first_not_of(" \t\r\n") == std::string::npos) {
      outcome = routing::Outcome::kModelFailure;
      error = "empty output";
    }
    record(outcome, error);

    if (outcome == routing::Outcome::kAborted) {
      out["error"] = "Subagent cancelled due to abort flag";
      return out;
    }
    if (outcome == routing::Outcome::kSuccess) {
      if (qcode::task_notes::resolve(workspace).exists) {
        const std::string label = !description.empty() ? description : prompt_text;
        qcode::task_notes::append_log(
            workspace, "sub:" + mode,
            "done (" + target_provider->id + ":" + target_model_id + "): " +
                label.substr(0, 120));
      }
      out = {{"output", res.text},
             {"provider", target_provider->id},
             {"model", target_model_id},
             {"attempts", attempt},
             {"route_reason", cand.reason}};
      if (cand.expected) out["expected"] = *cand.expected;
      return out;
    }
    last_error = error;
    LOG_WARN("subagent attempt {}/{} failed on {}:{}: {}", attempt, kSubagentMaxAttempts,
             target_provider->id, target_model_id, error);
    if (outcome == routing::Outcome::kTransientError && provider_wide_limit(error)) {
      limited_providers.insert(target_provider->id);
    }
  }  // end fallback loop
  out["error"] = last_error.empty() ? "subagent generation failed" : last_error;
  out["attempts"] = attempt;
  } catch (const std::exception& e) {
    out["error"] = std::string("subagent crashed: ") + e.what();
  }
  return out;
}

// ──────────────────────────────────────────────────────────────
//  Tools-enabled generation path (bus version)
// ──────────────────────────────────────────────────────────────
static void run_tools_generation_bus(
    qcode::Client& client,
    qcode::GenerateOptions gen_options,
    bus::BusPort& bus,
    GenerationContext& ctx,
    const std::function<bool(qcode::Client&)>& refresh_client = nullptr,
    const std::string& provider_id = "") {
  // One request serves every step. A StreamOptions is a GenerateOptions, so
  // streamed steps pass it to stream_text() without copying the history.
  qcode::StreamOptions request(std::move(gen_options));
  qcode::GenerateOptions& options = request;
  // Steps stream (text shows as the model writes it) when the client can;
  // QCODE_TOOL_STREAMING=0 keeps one blocking generate_text() per step.
  const char* tool_streaming_env = std::getenv("QCODE_TOOL_STREAMING");
  const bool tool_streaming =
      !(tool_streaming_env && std::string_view(tool_streaming_env) == "0");

  struct InFlightTool {
    std::chrono::steady_clock::time_point started;
    std::string tool_name;
  };
  auto tool_starts = std::make_shared<std::map<std::string, InFlightTool>>();
  auto callback_mutex    = std::make_shared<std::mutex>();
  auto step_counter      = std::make_shared<std::atomic<int>>(0);
  const int max_steps    = options.max_steps;

  // ── Tool call started: emit ToolCallStarted ──
  options.on_tool_call_start =
      [&bus, &ctx, tool_starts, step_counter, max_steps,
       callback_mutex](const qcode::ToolCall& call) {
        std::lock_guard<std::mutex> lock(*callback_mutex);
        LOG_DEBUG("generation_service: on_tool_call_start tool={} step={}/{}", call.tool_name, (int)*step_counter, max_steps);
        auto now = std::chrono::steady_clock::now();
        (*tool_starts)[call.id] = InFlightTool{now, call.tool_name};
        (*step_counter)++;

        bus.publish<ToolCallStarted>({
            .session_id = ctx.session_id,
            .tool_call_id = call.id,
            .tool_name = call.tool_name,
            .arguments = call.arguments
        });
      };

  // ── Tool call finished: emit ToolCallCompleted ──
  options.on_tool_call_finish =
      [&bus, &ctx, tool_starts,
       callback_mutex](const qcode::ToolResult& res) {
        std::lock_guard<std::mutex> lock(*callback_mutex);
        double duration_s = 0.0;
        auto start_it = tool_starts->find(res.tool_call_id);
        if (start_it != tool_starts->end()) {
          duration_s = std::chrono::duration<double>(
              std::chrono::steady_clock::now() - start_it->second.started).count();
          tool_starts->erase(start_it);
          LOG_DEBUG("generation_service: on_tool_call_finish tool={} success={} duration={:.1f}s", res.tool_name, res.is_success(), duration_s);
        } else {
          LOG_DEBUG("generation_service: on_tool_call_finish tool={} success={} duration=unknown", res.tool_name, res.is_success());
        }

        ctx.tool_call_count++;
        ctx.total_tool_time_ms += duration_s * 1000.0;

        bus.publish<ToolCallCompleted>({
            .session_id = ctx.session_id,
            .tool_call_id = res.tool_call_id,
            .tool_name = res.tool_name,
            .result = res.result,
            .is_error = !res.is_success(),
            .duration_ms = duration_s * 1000.0
        });
      };

  auto flush_inflight_tools = [&bus, &ctx, tool_starts, callback_mutex]() {
    std::lock_guard<std::mutex> lock(*callback_mutex);
    for (const auto& [id, info] : *tool_starts) {
      const double duration_ms =
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - info.started)
              .count();
      bus.publish<ToolCallCompleted>({
          .session_id = ctx.session_id,
          .tool_call_id = id,
          .tool_name = info.tool_name,
          .result = {{"error", "Tool execution aborted"}},
          .is_error = true,
          .duration_ms = duration_ms,
      });
    }
    tool_starts->clear();
  };

  // Assistant text of each finished step goes to the UI as one MessageDelta
  // (a streamed step published it already and is only counted).
  // Only its length is kept: the text itself lives in the request history.
  size_t text_chars = 0;
  auto publish_step_text = [&bus, &ctx, &text_chars](std::string text,
                                                     bool streamed = false) {
    if (text.empty()) return;
    text_chars += text.size();
    if (streamed) return;
    bus.publish<MessageDelta>({
        .session_id = ctx.session_id,
        .text = std::move(text),
        .done = false
    });
  };

  // ── Multi-step generation loop ──
  // Turn totals: usage, plus the error that ended the turn (if any).
  qcode::GenerateResult gen_result;

  // `options` is the request for every step. Each step's messages are
  // appended to its history in place, so no step re-copies the conversation
  // or its tool outputs. (The tool executor reads only tools, workspace,
  // session, abort flag and callbacks from it.)
  if (options.messages.empty() && !options.prompt.empty()) {
    options.messages.push_back(qcode::Message::user(options.prompt));
  }
  options.prompt.clear();
  options.max_steps = 1;
  options.on_retry = [&bus, &ctx](int attempt, int total_attempts,
                                  std::chrono::milliseconds delay,
                                  const std::string& err_msg) {
    double secs = delay.count() / 1000.0;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1fs", secs);
    const std::string why = format_user_facing_error(err_msg, 80);
    std::string toast_msg = "Retrying " + std::to_string(attempt) + "/" +
                            std::to_string(total_attempts) + " in " + buf;
    if (!why.empty()) toast_msg += " — " + why;
    bus.publish<contract::ErrorOccurred>({
        .session_id = ctx.session_id,
        .message = toast_msg,
        .severity = "info"
    });
  };

  // Heuristic token count of options.messages for the live context display
  // when the provider reports no prompt_tokens. Computed once on first use,
  // then grown by each appended message instead of re-walking the history.
  std::optional<size_t> est_history_tokens;
  qcode::Messages scratch;  // estimate_tokens() takes a vector: move through it
  auto append_message = [&options, &est_history_tokens,
                         &scratch](qcode::Message msg) -> size_t {
    scratch.push_back(std::move(msg));
    const size_t tokens = estimate_tokens(scratch);
    options.messages.push_back(std::move(scratch.back()));
    scratch.clear();
    if (est_history_tokens) *est_history_tokens += tokens;
    return tokens;
  };

  // A prompt queued while this turn runs joins the next model request instead
  // of waiting for the turn to end. Returns true when something was injected.
  auto inject_queued_prompts = [&]() -> bool {
    if (!ctx.take_queued_prompts) return false;
    auto prompts = ctx.take_queued_prompts();
    if (prompts.empty()) return false;
    std::string text;
    for (auto& p : prompts) {
      if (!text.empty()) text += "\n\n";
      text += std::move(p);
    }
    append_message(qcode::Message::user(text));
    bus.publish<UserMessageInjected>(
        {.session_id = ctx.session_id, .text = std::move(text)});
    return true;
  };

  // generate_text is a blocking HTTP POST. Without a heartbeat the TUI
  // sits on "generating" for the full read timeout and looks wedged.
  // The request is read by reference: this thread leaves `options`
  // untouched until fut.get() (and ~future joins on unwind).
  auto generate_step = [&]() {
    auto fut = std::async(std::launch::async, [&client, &options, sid = ctx.session_id]() {
      qcode::logger::ScopedThreadSession bind(sid);
      return client.generate_text(options);
    });
    int waited_sec = 0;
    while (fut.wait_for(std::chrono::seconds(15)) !=
           std::future_status::ready) {
      waited_sec += 15;
      const bool stopping =
          ctx.abort_flag && ctx.abort_flag->load();
      bus.publish<contract::ErrorOccurred>({
          .session_id = ctx.session_id,
          .message = stopping
                         ? "Stopping… waiting for the network call (" +
                               std::to_string(waited_sec) + "s)"
                         : "Still waiting for the model (" +
                               std::to_string(waited_sec) + "s)…",
          .severity = "info",
      });
    }
    return fut.get();
  };

  int step = 0;
  bool finished = false;
  bool aborted = false;
  bool stuck = false;
  bool auth_retried = false;
  int auto_continues = 0;
  // Consecutive steps with the same tool calls AND the same results.
  // Same-args retries with changing output (re-read, poll) are allowed.
  int no_progress_repeat = 0;
  std::optional<size_t> last_progress_fp;

  double turn_tools_ms = 0.0;
  double turn_model_ms = 0.0;
  auto turn_start = std::chrono::steady_clock::now();
  int turn_steps = 0;

  LOG_DEBUG("run_tools_generation_bus: starting loop max_steps={}", max_steps);
  while (!finished && (max_steps <= 0 || step < max_steps)) {
    if (ctx.abort_flag && ctx.abort_flag->load()) {
      LOG_INFO("run_tools_generation_bus: abort requested at step={}", step);
      aborted = true;
      break;
    }
    LOG_DEBUG("run_tools_generation_bus: step={} messages={}", step, options.messages.size());

    {
      // Streamed steps publish reasoning and text while the model writes.
      const bool streamed = tool_streaming && client.supports_tool_streaming();
      LOG_DEBUG("run_tools_generation_bus: step={} {}", step,
                streamed ? "stream_text" : "sync generate_text");
      const qcode::perf::Stopwatch model_watch;
      qcode::GenerateResult step_res =
          streamed ? stream_step(client, request, bus, ctx) : generate_step();
      // Latency is measured here, before tools run, so it is pure model time.
      const double step_model_ms = model_watch.ms();
      turn_model_ms += step_model_ms;
      turn_steps++;
      {
        int step_think = step_res.usage.reasoning_completion_tokens;
        if (step_think == 0 && !step_res.reasoning.empty()) {
          step_think = std::max(1, static_cast<int>(step_res.reasoning.size() / 4));
        }
        const double ttft = step_res.ttft_ms.value_or(-1.0);
        const std::string effort = options.reasoning_effort.value_or("off");
        const std::string variant = options.reasoning_variant.value_or("");
        // Persist before publishing: the Stats tab mirror reloads from the DB
        // on session switches and must not miss (or double count) this call.
        qcode::session::record_session_model_call(
            ctx.session_id,
            {.model_ms = step_model_ms,
             .ttft_ms = ttft,
             .input_tokens = step_res.usage.prompt_tokens,
             .cache_read_tokens = step_res.usage.cached_prompt_tokens,
             .cache_write_tokens = step_res.usage.cache_write_tokens,
             .output_tokens = step_res.usage.completion_tokens,
             .reasoning_tokens = step_think,
             .effort = effort,
             .variant = variant});
        bus.publish<qcode::contract::StepLatency>({
            .session_id = ctx.session_id,
            .step = step,
            .streamed = streamed,
            .model_ms = step_model_ms,
            .ttft_ms = ttft,
            .output_tokens = step_res.usage.completion_tokens,
            .reasoning_tokens = step_think,
            .effort = effort,
            .ok = step_res.is_success(),
            .input_tokens = step_res.usage.prompt_tokens,
            .cache_read_tokens = step_res.usage.cached_prompt_tokens,
            .cache_write_tokens = step_res.usage.cache_write_tokens,
            .variant = variant,
        });
        const double tok_per_s =
            step_model_ms > 0 ? step_res.usage.completion_tokens / (step_model_ms / 1000.0) : 0.0;
        LOG_INFO("[latency] step={} mode={} model_ms={:.0f} ttft_ms={:.0f} "
                 "out_tokens={} think_tokens={} tok_per_s={:.1f} effort={} "
                 "variant={} in_tokens={} cache_read={} cache_write={} "
                 "model={} ok={}",
                 step, streamed ? "stream" : "sync", step_model_ms, ttft,
                 step_res.usage.completion_tokens, step_think, tok_per_s,
                 effort, variant, step_res.usage.prompt_tokens,
                 step_res.usage.cached_prompt_tokens,
                 step_res.usage.cache_write_tokens, options.model,
                 step_res.is_success());
      }
      PERF_LOG("step={} model_ms={:.1f} history_msgs={} prompt_tokens={} "
               "completion_tokens={} cached_tokens={}",
               step, model_watch.ms(), options.messages.size(),
               step_res.usage.prompt_tokens, step_res.usage.completion_tokens,
               step_res.usage.cached_prompt_tokens);
      if (!step_res.is_success()) {
        const std::string err = step_res.error_message();
        LOG_ERROR("run_tools_generation_bus: step={} generate_text failed: finish_reason={} error=\"{}\" provider_metadata_size={}", step, step_res.finishReasonToString(), err, step_res.provider_metadata.value_or("").size());
        // RetryPolicy returns this only when Esc cut the schedule. A hung
        // socket that later dies is a real error — show it even if Esc
        // was pressed because the UI looked wedged.
        if (err.find("Aborted by user") != std::string::npos) {
          LOG_INFO("run_tools_generation_bus: aborted at step={}", step);
          aborted = true;
          break;
        }
        // A streamed step that already showed output is not replayed: the
        // retry would show it twice.
        const bool shown = streamed && (!step_res.text.empty() ||
                                        !step_res.reasoning.empty());
        if (!auth_retried && !shown && looks_like_auth_error(err) &&
            refresh_client && refresh_client(client)) {
          auth_retried = true;
          LOG_WARN("run_tools_generation_bus: refreshed credentials, retrying step={}",
                   step);
          continue;
        }
        if (step_res.finish_reason == qcode::kFinishReasonContentFilter) {
          gen_result.error =
              "Provider blocked this turn (content filter). Rephrase the "
              "prompt or switch models.";
        } else if (looks_like_auth_error(err)) {
          // Only blame Antigravity credentials when we are actually talking to
          // Antigravity — a Zen/OpenRouter ModelError(401) must not ask the
          // user to re-login with a unrelated CLI.
          if (provider_id.find("antigravity") != std::string::npos) {
            gen_result.error =
                "Authentication failed (401). Re-login with the Antigravity CLI "
                "or set ANTIGRAVITY_API_KEY, then try again. Original: " +
                err;
          } else {
            gen_result.error = "Authentication failed (401) for provider '" +
                               provider_id +
                               "'. Check its API key. Original: " + err;
          }
        } else {
          gen_result.error = !err.empty() ? err : "Provider request failed";
        }
        gen_result.provider_metadata = std::move(step_res.provider_metadata);
        break;
      }
      if (ctx.abort_flag && ctx.abort_flag->load()) {
        LOG_INFO("run_tools_generation_bus: abort after generate_text step={}",
                 step);
        aborted = true;
        break;
      }

      // Thinking tokens from this step: publish as a reasoning event BEFORE
      // the text delta so the TUI renders the thinking block above the
      // assistant text (opencode-style), and replay it in later requests.
      // A streamed step published them as they arrived.
      const size_t reasoning_chars = step_res.reasoning.size();
      if (reasoning_chars > 0 && !streamed) {
        bus.publish<ReasoningDelta>({
            .session_id = ctx.session_id,
            .text = std::move(step_res.reasoning),
            .signature = "",
            .done = true,
        });
      }

      gen_result.usage.prompt_tokens = step_res.usage.prompt_tokens;
      gen_result.usage.completion_tokens += step_res.usage.completion_tokens;
      gen_result.usage.total_tokens =
          gen_result.usage.prompt_tokens + gen_result.usage.completion_tokens;
      if (step_res.usage.prompt_tokens > 0) {
        bus.publish<ContextSizeUpdated>({
            .context_tokens = static_cast<int>(step_res.usage.prompt_tokens),
        });
      }
      // Prompt-cache accounting: keep the max seen (each step reports the
      // cached prefix of that request, not a delta).
      gen_result.usage.cached_prompt_tokens =
          std::max(gen_result.usage.cached_prompt_tokens,
                   step_res.usage.cached_prompt_tokens);
      // Thinking-token accounting: sum across steps since each step outputs a new thinking block.
      if (step_res.usage.reasoning_completion_tokens > 0) {
        gen_result.usage.reasoning_completion_tokens += step_res.usage.reasoning_completion_tokens;
      } else if (reasoning_chars > 0) {
        gen_result.usage.reasoning_completion_tokens += std::max(1, static_cast<int>(reasoning_chars / 4));
      }
      // Live header: TokenUsageUpdated at loop end is too late for a
      // 30-step tool run. Zeros keep session totals from double-counting.
      if (gen_result.usage.reasoning_completion_tokens > 0 ||
          step_res.usage.cached_prompt_tokens > 0) {
        bus.publish<TokenUsageUpdated>({
            .prompt_tokens = 0,
            .completion_tokens = 0,
            .total_tokens = 0,
            .cached_prompt_tokens = step_res.usage.cached_prompt_tokens,
            .reasoning_tokens = gen_result.usage.reasoning_completion_tokens,
            .session_id = ctx.session_id,
        });
      }

      LOG_DEBUG("run_tools_generation_bus: step={} has_tool_calls={} text_len={}", step, step_res.has_tool_calls(), step_res.text.size());
      if (step_res.has_tool_calls()) {
        // Execute tool calls to produce tool results
        const qcode::perf::Stopwatch tools_watch;
        std::vector<qcode::ToolResult> executed_results =
            qcode::ToolExecutor::execute_tools_with_options(step_res.tool_calls, options, /*parallel=*/true);
        double step_tools_ms = tools_watch.ms();
        turn_tools_ms += step_tools_ms;
        PERF_LOG("step={} tools_ms={:.1f} tool_calls={}", step, step_tools_ms,
                 step_res.tool_calls.size());

        // Detect true no-progress wedges only: identical calls *and* identical
        // results. Do not stop on long tool-only streaks or same-args retries
        // that return new data. Uncapped: instead of halting, inject a
        // corrective nudge so the model breaks the repetition itself.
        // (Fingerprint before the calls/results are moved into history.)
        const size_t progress_fp =
            tool_step_fingerprint(step_res.tool_calls, executed_results);
        if (last_progress_fp == progress_fp) {
          ++no_progress_repeat;
        } else {
          last_progress_fp = progress_fp;
          no_progress_repeat = 1;
        }

        // History: the assistant tool-call turn, then its results. Both are
        // moved in; tool outputs can be large.
        if (!step_res.response_messages.empty()) {
          // Parser already attached the reasoning part alongside tool calls —
          // reuse it so thinking replays across steps.
          append_message(std::move(step_res.response_messages.front()));
        } else {
          // Message::assistant_with_tools, without copying the arguments.
          qcode::MessageContent call_parts;
          call_parts.reserve(step_res.tool_calls.size() + 1);
          if (!step_res.text.empty()) {
            call_parts.emplace_back(qcode::TextContentPart{step_res.text});
          }
          for (auto& call : step_res.tool_calls) {
            call_parts.emplace_back(qcode::ToolCallContentPart{
                std::move(call.id), std::move(call.tool_name),
                std::move(call.arguments), std::move(call.thought_signature)});
          }
          append_message(qcode::Message(qcode::kMessageRoleAssistant,
                                        std::move(call_parts)));
        }
        // Message::tool_results, without copying the outputs.
        qcode::MessageContent result_parts;
        result_parts.reserve(executed_results.size());
        for (auto& res : executed_results) {
          const bool is_error = !res.is_success();
          result_parts.emplace_back(qcode::ToolResultContentPart{
              std::move(res.tool_call_id), std::move(res.result), is_error});
        }
        const size_t tool_res_tok = append_message(
            qcode::Message(qcode::kMessageRoleUser, std::move(result_parts)));

        constexpr int kNoProgressNudgeAfter = 2;
        constexpr int kMaxNoProgressRepeats = 4;
        if (no_progress_repeat >= kMaxNoProgressRepeats) {
          LOG_WARN(
              "run_tools_generation_bus: no-progress tool loop stuck after {} "
              "identical repeats (step={}); stopping loop",
              no_progress_repeat, step);
          stuck = true;
        } else if (no_progress_repeat >= kNoProgressNudgeAfter) {
          LOG_WARN(
              "run_tools_generation_bus: no-progress tool loop repeating "
              "(no_progress_repeat={} step={}); nudging for a different approach",
              no_progress_repeat, step);
          append_message(qcode::Message::user(
              "[System Note: Your previous tool call produced the exact same result. "
              "Do not repeat the identical call or command; change parameters, "
              "try a different approach, inspect a different file, or conclude if finished.]"));
        }

        // Re-publish the live context size after each tool call so the TUI's
        // context window updates dynamically as messages are appended.
        {
            size_t live = 0;
            if (step_res.usage.prompt_tokens > 0) {
                live = step_res.usage.prompt_tokens + tool_res_tok;
            } else {
                if (!est_history_tokens) {
                    est_history_tokens = estimate_tokens(options.messages);
                }
                live = estimate_system_tokens(options.system) + *est_history_tokens;
            }
            bus.publish<ContextSizeUpdated>({.context_tokens = static_cast<int>(live)});
            LOG_INFO("run_tools_generation_bus: step={} live context={} tokens", step, live);
        }

        publish_step_text(std::move(step_res.text), streamed);
        if (stuck) break;
        inject_queued_prompts();
      } else {
        no_progress_repeat = 0;
        last_progress_fp.reset();
        const size_t text_len = step_res.text.size();
        const bool wants_continue =
            should_auto_continue_build(auto_continues, step_res.text);
        append_message(qcode::Message::assistant(step_res.text));
        publish_step_text(std::move(step_res.text), streamed);
        if (inject_queued_prompts()) {
          // The queued prompt is the next request's input; keep going.
        } else if (wants_continue) {
          ++auto_continues;
          LOG_INFO(
              "run_tools_generation_bus: auto-continue {} after text-only "
              "stop (text_len={})",
              auto_continues, text_len);
          append_message(qcode::Message::user(std::string(kBuildContinueNudge)));
        } else {
          LOG_DEBUG(
              "run_tools_generation_bus: step={} no tool calls, finishing",
              step);
          finished = true;
        }
      }


    }
    step++;
  }

  LOG_DEBUG("run_tools_generation_bus: loop complete steps={} total_text_len={} tool_calls={} aborted={} stuck={}",
           step, text_chars, ctx.tool_call_count, aborted, stuck);

  // If we reached the tool step limit without producing any text, synthesize a
  // final textual summary of the findings with tools disabled so the turn completes
  // naturally instead of leaving the user with an empty error.
  const bool capped = (max_steps > 0);
  if (!aborted && !stuck && !finished && capped && step >= max_steps &&
      text_chars == 0) {
    LOG_INFO("run_tools_generation_bus: step cap reached ({} steps); synthesizing final response without tools",
             max_steps);
    // The loop is over: reuse its request with tools off and a closing note.
    options.tools.clear();
    options.on_retry.reset();
    options.messages.push_back(qcode::Message::user(
        "[System Note: You have reached the maximum tool steps for this turn. Please summarize your progress, key findings, what changes were made, and your next recommended steps directly to the user now without requesting any further tools.]"
    ));

    auto synth_res = client.generate_text(options);
    if (synth_res.is_success() && !synth_res.text.empty()) {
      finished = true;
      gen_result.usage.prompt_tokens = synth_res.usage.prompt_tokens;
      gen_result.usage.completion_tokens += synth_res.usage.completion_tokens;
      gen_result.usage.total_tokens =
          gen_result.usage.prompt_tokens + gen_result.usage.completion_tokens;
      publish_step_text(std::move(synth_res.text));
    }
  }

  if (aborted) {
    flush_inflight_tools();
    bus.publish<ErrorOccurred>({
        .session_id = ctx.session_id,
        .message = "Generation stopped",
        .severity = "warning"
    });
    bus.publish<MessageDelta>({
        .session_id = ctx.session_id,
        .text = "",
        .done = true
    });
    bus.publish<SessionStatusChanged>({
        .session_id = ctx.session_id,
        .status = "idle"
    });
    return;
  }

  if (stuck) {
    bus.publish<ErrorOccurred>({
        .session_id = ctx.session_id,
        .message =
            "Stopped: tool calls produced no new progress (same calls and "
            "results repeated). Try a clearer prompt, /compact, or press r "
            "to retry.",
        .severity = "warning"
    });
    bus.publish<MessageDelta>({
        .session_id = ctx.session_id,
        .text = "",
        .done = true
    });
    bus.publish<SessionStatusChanged>({
        .session_id = ctx.session_id,
        .status = "idle"
    });
    return;
  }

  // Every non-error exit leaves gen_result successful, so the error string
  // alone decides the outcome.
  const bool fatal_error =
      gen_result.error.has_value() && !gen_result.error->empty();

  if (fatal_error) {
    const std::string raw = gen_result.error_message();
    if (gen_result.provider_metadata.has_value() &&
        !gen_result.provider_metadata->empty()) {
      LOG_ERROR("run_tools_generation_bus: error occurred: {} [Response] {}",
                raw, gen_result.provider_metadata->substr(0, 500));
    } else {
      LOG_ERROR("run_tools_generation_bus: error occurred: {}", raw);
    }
    bus.publish<ErrorOccurred>({
        .session_id = ctx.session_id,
        .message = format_user_facing_error(raw),
        .severity = "error"
    });
    bus.publish<MessageDelta>({
        .session_id = ctx.session_id,
        .text = "",
        .done = true
    });
  } else if (!finished && capped && step >= max_steps) {
    if (text_chars == 0) {
      publish_step_text(
          "I reached the tool execution limit (" + std::to_string(max_steps) +
          " steps) for this turn while working on your request. Send 'continue' to proceed with the next steps.");
    }
    bus.publish<MessageDelta>({
        .session_id = ctx.session_id,
        .text = "",
        .done = true
    });
  } else if (text_chars > 0) {
    LOG_DEBUG("run_tools_generation_bus: publishing final MessageDelta text_len={}", text_chars);
    bus.publish<MessageDelta>({
        .session_id = ctx.session_id,
        .text = "",
        .done = true
    });
  } else {
    // LLM produced no text and no tool output. Surface a non-error notice
    // instead of leaving the user with a silently blank turn.
    LOG_WARN("run_tools_generation_bus: model returned empty response (no text, no tool output)");
    bus.publish<ErrorOccurred>({
        .session_id = ctx.session_id,
        .message = "The model returned an empty response (no text generated).",
        .severity = "warning"
    });
  }

  {
    const double wall_ms = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - turn_start)
                               .count();
    LOG_INFO("[latency] turn steps={} model_ms_total={:.0f} tool_ms_total={:.0f} "
             "wall_ms={:.0f} out_tokens={} think_tokens_total={} effort={} "
             "model={} ok={}",
             turn_steps, turn_model_ms, turn_tools_ms, wall_ms,
             gen_result.usage.completion_tokens,
             gen_result.usage.reasoning_completion_tokens,
             options.reasoning_effort.value_or("off"), options.model,
             !fatal_error && !aborted);
    if (!aborted) {
      qcode::session::record_generation_turn(options.model, provider_id,
                                             !fatal_error, stuck,
                                             turn_steps > 0 ? turn_model_ms / turn_steps : 0.0);
    }
  }
  bus.publish<TokenUsageUpdated>({
      .prompt_tokens = gen_result.usage.prompt_tokens,
      .completion_tokens = gen_result.usage.completion_tokens,
      .total_tokens = gen_result.usage.total_tokens,
      .cached_prompt_tokens = gen_result.usage.cached_prompt_tokens,
      .reasoning_tokens = gen_result.usage.reasoning_completion_tokens,
      .session_id = ctx.session_id,
  });
  if (!fatal_error) {
    bus.publish<SessionStatusChanged>({
        .session_id = ctx.session_id,
        .status = "idle"
    });
  }
}

// ──────────────────────────────────────────────────────────────
//  Non-tools streaming generation path (bus version)
// ──────────────────────────────────────────────────────────────
static void run_stream_generation_bus(qcode::Client& client,
                                       qcode::GenerateOptions gen_options,
                                       bus::BusPort& bus,
                                       GenerationContext& ctx,
                                       const std::string& provider_id = "") {
  qcode::StreamOptions stream_options(std::move(gen_options));
  auto gen_start_time = std::chrono::high_resolution_clock::now();
  auto stream = client.stream_text(stream_options);
  LOG_DEBUG("run_stream_generation_bus: streaming model={} system={}", stream_options.model, stream_options.system.size());

  // Do not call stream.has_error()/error_message() here: those iterate the
  // whole stream and would discard Cursor/Grok text deltas before the UI
  // loop below can publish them.

  std::string text_buffer;
  size_t text_size = 0;
  auto last_text_flush = std::chrono::steady_clock::now();
  constexpr auto kFlushInterval = std::chrono::milliseconds(33);


  double ttft_ms = -1.0;
  int stream_out_tokens = 0;
  int stream_think_tokens = 0;
  int stream_in_tokens = 0;
  int stream_cache_read = 0;
  int stream_cache_write = 0;
  bool stream_ok = true;

  auto flush_text = [&]() {

    if (text_buffer.empty()) return;
    LOG_DEBUG("ChatBus: flush_text buffer_size={}", text_buffer.size());
    bus.publish<MessageDelta>({
        .session_id = ctx.session_id,
        .text = std::move(text_buffer),
        .done = false
    });
    text_buffer.clear();
  };

  std::string reasoning_buffer;
  size_t reasoning_chars = 0;
  bool has_reasoning = false;
  std::string last_reasoning_signature;
  auto last_reasoning_flush = std::chrono::steady_clock::now();
  auto flush_reasoning = [&]() {
    if (reasoning_buffer.empty()) return;
    LOG_DEBUG("ChatBus: flush_reasoning buffer_size={}", reasoning_buffer.size());
    bus.publish<ReasoningDelta>({
        .session_id = ctx.session_id,
        .text = std::move(reasoning_buffer),
        .signature = last_reasoning_signature,
        .done = false
    });
    reasoning_buffer.clear();
  };

  bool aborted = false;
  for (const auto& event : stream) {
    if (ctx.abort_flag && ctx.abort_flag->load()) {
      LOG_INFO("run_stream_generation_bus: abort requested");
      aborted = true;
      stream.stop();
      break;
    }
    if (ctx.has_queued_work && ctx.has_queued_work()) {
      LOG_INFO(
          "run_stream_generation_bus: queued prompt pending; ending turn "
          "so it can start");
      stream.stop();
      break;
    }

    if (event.is_text_delta()) {
      if (ttft_ms < 0.0) {
        ttft_ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - gen_start_time).count();
      }

      text_buffer += event.text_delta;
      text_size += event.text_delta.size();
      auto now = std::chrono::steady_clock::now();
      if (now - last_text_flush >= kFlushInterval) {
        flush_text();
        last_text_flush = now;
      }

    } else if (event.is_reasoning_delta()) {
      if (ttft_ms < 0.0) {
        ttft_ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - gen_start_time).count();
      }

      // OpenRouter encrypts reasoning as [REDACTED]; drop those chunks.
      const std::string& chunk = event.text_delta;
      if (chunk.find("[REDACTED]") != std::string::npos) {
        LOG_DEBUG("ChatBus: dropping redacted reasoning chunk");
      } else {
        reasoning_buffer += chunk;
        reasoning_chars += chunk.size();
        has_reasoning = true;
        if (event.metadata.has_value() && !event.metadata->empty()) {
          last_reasoning_signature = *event.metadata;
        }
        LOG_DEBUG("ChatBus: reasoning_delta buffer_size={}", reasoning_buffer.size());
        auto now = std::chrono::steady_clock::now();
        if (now - last_reasoning_flush >= kFlushInterval) {
          flush_reasoning();
          last_reasoning_flush = now;
        }
      }
    } else if (event.is_tool_call()) {
      flush_text();
      nlohmann::json args = nlohmann::json::object();
      if (!event.tool_payload.empty()) {
        try {
          args = nlohmann::json::parse(event.tool_payload);
        } catch (...) {
          args = event.tool_payload;
        }
      }
      bus.publish<ToolCallStarted>({
          .session_id = ctx.session_id,
          .tool_call_id = event.tool_call_id,
          .tool_name = event.tool_name,
          .arguments = std::move(args),
      });
    } else if (event.is_tool_result()) {
      nlohmann::json result = nlohmann::json::object();
      if (!event.tool_payload.empty()) {
        try {
          result = nlohmann::json::parse(event.tool_payload);
        } catch (...) {
          result = event.tool_payload;
        }
      }
      bus.publish<ToolCallCompleted>({
          .session_id = ctx.session_id,
          .tool_call_id = event.tool_call_id,
          .tool_name = event.tool_name,
          .result = std::move(result),
          .is_error = event.tool_is_error,
          .duration_ms = 0.0,
      });
    } else if (event.is_error()) {
      const std::string raw =
          (event.error.has_value() && !event.error->empty())
              ? *event.error
              : "Error during streaming";
      LOG_ERROR("run_stream_generation_bus: stream error: {}", raw);
      flush_text();
      flush_reasoning();
      bus.publish<ErrorOccurred>({
          .session_id = ctx.session_id,
          .message = format_user_facing_error(raw),
          .severity = "error"
      });

      double latency_ms = std::chrono::duration<double, std::milli>(
                              std::chrono::high_resolution_clock::now() -
                              gen_start_time)
                              .count();
      stream_ok = false;
      qcode::session::record_session_model_call(
          ctx.session_id,
          {.model_ms = latency_ms,
           .ttft_ms = ttft_ms,
           .input_tokens = stream_in_tokens,
           .cache_read_tokens = stream_cache_read,
           .cache_write_tokens = stream_cache_write,
           .output_tokens = stream_out_tokens,
           .reasoning_tokens = stream_think_tokens,
           .effort = stream_options.reasoning_effort.value_or("off"),
           .variant = stream_options.reasoning_variant.value_or("")});
      bus.publish<qcode::contract::StepLatency>({
          .session_id = ctx.session_id,
          .step = 1,
          .streamed = true,
          .model_ms = latency_ms,
          .ttft_ms = ttft_ms,
          .output_tokens = stream_out_tokens,
          .reasoning_tokens = stream_think_tokens,
          .effort = stream_options.reasoning_effort.value_or("off"),
          .ok = stream_ok,
          .input_tokens = stream_in_tokens,
          .cache_read_tokens = stream_cache_read,
          .cache_write_tokens = stream_cache_write,
          .variant = stream_options.reasoning_variant.value_or(""),
      });
      LOG_INFO("[latency] turn steps=1 mode=stream model_ms_total={:.0f} ttft_ms={:.0f} "
               "out_tokens={} think_tokens_total={} effort={} model={} ok=false",
               latency_ms, ttft_ms, stream_out_tokens, stream_think_tokens,
               stream_options.reasoning_effort.value_or("off"), stream_options.model);

      qcode::session::record_generation_turn(stream_options.model, provider_id,
                                             false, false, latency_ms);
      return;

    } else if (event.is_finish() && event.usage.has_value()) {
      LOG_DEBUG("run_stream_generation_bus: stream finished text_len={}", text_buffer.size());
      flush_text();
      int think_tokens = event.usage->reasoning_completion_tokens;
      if (think_tokens == 0 && reasoning_chars > 0) {
        think_tokens = std::max(1, static_cast<int>(reasoning_chars / 4));
      }
      stream_out_tokens = event.usage->completion_tokens;
      stream_think_tokens = think_tokens;
      stream_in_tokens = event.usage->prompt_tokens;
      stream_cache_read = event.usage->cached_prompt_tokens;
      stream_cache_write = event.usage->cache_write_tokens;
      bus.publish<TokenUsageUpdated>({
          .prompt_tokens = event.usage->prompt_tokens,
          .completion_tokens = event.usage->completion_tokens,
          .total_tokens = event.usage->total_tokens,
          .cached_prompt_tokens = event.usage->cached_prompt_tokens,
          .reasoning_tokens = think_tokens,
          .session_id = ctx.session_id,
      });
    }
  }

  flush_text();
  flush_reasoning();
  if (aborted) {
    bus.publish<ErrorOccurred>({
        .session_id = ctx.session_id,
        .message = "Generation stopped",
        .severity = "warning"
    });
  } else if (text_size == 0 && !has_reasoning) {
    LOG_WARN(
        "run_stream_generation_bus: empty response model={} text_len=0",
        stream_options.model);
    bus.publish<ErrorOccurred>({
        .session_id = ctx.session_id,
        .message = "The model returned an empty response (no text generated).",
        .severity = "warning"
    });
  }
  LOG_INFO(
      "run_stream_generation_bus: complete text_len={} reasoning_chars={} "
      "aborted={}",
      text_size, reasoning_chars, aborted);
  {
    const double latency_ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::high_resolution_clock::now() -
                                  gen_start_time)
                                  .count();
    const std::string effort = stream_options.reasoning_effort.value_or("off");
    qcode::session::record_session_model_call(
        ctx.session_id,
        {.model_ms = latency_ms,
         .ttft_ms = ttft_ms,
         .input_tokens = stream_in_tokens,
         .cache_read_tokens = stream_cache_read,
         .cache_write_tokens = stream_cache_write,
         .output_tokens = stream_out_tokens,
         .reasoning_tokens = stream_think_tokens,
         .effort = effort,
         .variant = stream_options.reasoning_variant.value_or("")});
    bus.publish<qcode::contract::StepLatency>({
        .session_id = ctx.session_id,
        .step = 0,
        .streamed = true,
        .model_ms = latency_ms,
        .ttft_ms = ttft_ms,
        .output_tokens = stream_out_tokens,
        .reasoning_tokens = stream_think_tokens,
        .effort = effort,
        .ok = !aborted,
        .input_tokens = stream_in_tokens,
        .cache_read_tokens = stream_cache_read,
        .cache_write_tokens = stream_cache_write,
        .variant = stream_options.reasoning_variant.value_or(""),
    });
    LOG_INFO("[latency] turn steps=1 mode=stream model_ms_total={:.0f} ttft_ms={:.0f} "
             "out_tokens={} think_tokens_total={} effort={} model={} ok={}",
             latency_ms, ttft_ms, stream_out_tokens, stream_think_tokens,
             effort, stream_options.model, !aborted);
    if (!aborted) {
      qcode::session::record_generation_turn(stream_options.model, provider_id,
                                             true, false, latency_ms);
    }
  }
  bus.publish<MessageDelta>({
      .session_id = ctx.session_id,
      .text = "",
      .done = true
  });

  if (has_reasoning) {
    bus.publish<ReasoningDelta>({
        .session_id = ctx.session_id,
        .text = "",
        .signature = last_reasoning_signature,
        .done = true
    });
  }

  bus.publish<SessionStatusChanged>({
      .session_id = ctx.session_id,
      .status = "idle"
  });
}

// ──────────────────────────────────────────────────────────────
//  Public entry point — resolve a provider Client, then generate
// ──────────────────────────────────────────────────────────────
void run_generation_with_bus(
    const std::string& provider_name,
    const std::string& model_id,
    const std::string& system_prompt,
    qcode::Messages messages,
    bool enable_tools,
    const std::vector<ProviderInfo>& providers,
    bus::BusPort& bus,
    GenerationContext& ctx)
{
  // Always return the session to idle, even on early auth/resolve failures.
  struct IdleGuard {
    bus::BusPort& bus;
    std::string session_id;
    ~IdleGuard() {
      bus.publish<SessionStatusChanged>({
          .session_id = session_id,
          .status = "idle",
      });
    }
  } idle_guard{bus, ctx.session_id};
  (void)idle_guard;

  try {
    bus.publish<SessionStatusChanged>({
        .session_id = ctx.session_id,
        .status = "generating"
    });

    // ── Resolve provider & API key ──
    qcode::providers::register_authenticated_providers();
    qcode::Client client;
    std::string provider_id;
    std::string resolved_model_id = model_id;
    const qcode::ModelInfo* resolved_model = nullptr;
    qcode::providers::ProviderOptions provider_options;

    for (const auto& p : providers) {
      if (p.id == provider_name || p.name == provider_name) {
        provider_id = p.id;
        provider_options.base_url = p.api_url;
        provider_options.api_key = p.api_key;
        provider_options.headers = p.headers;
        provider_options.protocol = p.protocol;
        provider_options.project_id = p.project_id;
        for (const auto& m : p.models) {
          if (m.id == model_id || m.name == model_id ||
              ProviderTransform::cursor_picker_id(model_id) == m.id ||
              ProviderTransform::cursor_picker_id(m.id) == model_id) {
            resolved_model_id = m.id;
            resolved_model = &m;
            if (!m.protocol.empty()) {
              provider_options.protocol = m.protocol;
            }
            break;
          }
        }
        break;
      }
    }
    if (provider_id == "cursor" && provider_options.api_key.empty()) {
      provider_options.api_key = get_cursor_access_token();
    } else if ((provider_id == "anthropic" || provider_name == "Anthropic") &&
               (provider_options.api_key.empty() || provider_options.api_key.starts_with("sk-ant-oat"))) {
      provider_options.api_key = get_anthropic_token(/*force_refresh=*/false);
    } else if ((provider_id == "openai" || provider_name == "OpenAI") &&
               provider_options.api_key.empty()) {
      const char* key = std::getenv("OPENAI_API_KEY");
      if (key && *key) {
        provider_options.api_key = key;
      }
    }

    const auto call = prepare_provider_call(
        provider_options, provider_id, resolved_model_id, ctx.session_id);
    resolved_model_id = call.wire_model_id;

    LOG_INFO("generation_service: provider={} model={} protocol={} path={} api={}",
             provider_id, resolved_model_id, provider_options.protocol,
             provider_options.completions_path.empty()
                 ? "(default)"
                 : provider_options.completions_path,
             provider_options.base_url);

    if (provider_id.empty()) {
      const std::string msg = provider_name.empty()
                                  ? "No provider selected"
                                  : ("Unknown provider: " + provider_name);
      bus.publish<ErrorOccurred>({
          .session_id = ctx.session_id,
          .message = msg,
          .severity = "error"
      });
      return;
    }

    // Provider registry is populated once at startup (see main.cpp). Resolve the
    // provider client via the central registry (auth + base URL handled per
    // provider). Errors are surfaced on the bus.
    auto resolution =
        qcode::providers::ProviderRegistry::instance().resolve(provider_id,
                                                             provider_options);
    if (!resolution.ok()) {
      bus.publish<ErrorOccurred>({
          .session_id = ctx.session_id,
          .message = resolution.error,
          .severity = "error"
      });
      return;
    }
    client = std::move(resolution.client);

    if (resolved_model_id.empty()) {
      bus.publish<ErrorOccurred>({
          .session_id = ctx.session_id,
          .message =
              "No model id selected for provider '" + provider_id +
              "'. Pick a model with /model — OpenCode Zen rejects an empty "
              "model field (401 ModelError).",
          .severity = "error"});
      return;
    }

    LOG_INFO("ChatBus: provider={}, model={}, tools={}", provider_id, resolved_model_id, enable_tools);

    // ── Build common base options ──
    qcode::GenerateOptions base_opts;
    base_opts.model = resolved_model_id;

    // ── Agent mode: orchestrator (default) or subagent (child session) ──
    const bool is_subagent = (ctx.agent_mode == "subagent") ||
                             (!ctx.session_id.empty() && qcode::session::is_child_session(ctx.session_id));
    // Prompt-cache prefix: single source of truth shared with compaction
    // (build_cache_replay_request) so both requests stay byte-identical.
    base_opts.system = build_turn_system_prompt(system_prompt, is_subagent, providers);
    if (is_subagent) {
        LOG_INFO("ChatBus: agent_mode=subagent (focused worker, nested delegation disabled)");
    } else {
        LOG_INFO("ChatBus: agent_mode=orchestrator (lead coordinator with parallel subagents)");
    }

    Model transform_model(resolved_model_id, provider_id);
    base_opts.messages = ProviderTransform::normalize_messages(std::move(messages), transform_model);
    if (!base_opts.temperature.has_value()) {
      base_opts.temperature = ProviderTransform::temperature(transform_model);
    }
    if (!base_opts.top_p.has_value()) {
      base_opts.top_p = ProviderTransform::top_p(transform_model);
    }
    if (resolved_model && resolved_model->output_limit > 0 && !base_opts.max_tokens.has_value()) {
      base_opts.max_tokens = ProviderTransform::max_output_tokens(resolved_model->output_limit);
    }
    base_opts.workspace = ctx.workspace;
    base_opts.session_id = ctx.session_id;
    base_opts.abort_flag = ctx.abort_flag;
    base_opts.has_queued_work = ctx.has_queued_work;

    // ── Extended thinking / reasoning ──
    // Session /variant (or the model's configured default when unset),
    // resolved through opencode.json; see apply_variant_options.
    apply_variant_options(base_opts, resolved_model, ctx.reasoning_mode,
                          resolved_model_id);

    // ── Dispatch ──
    // ServerSideDuplex providers (e.g. Cursor AgentService) own their autonomous tool loop.
    // Wrapping them in qcode's local tool loop is useless (no tool_calls returned)
    // and can truncate multi-step agent turns. Always stream ServerSideDuplex providers.
    // Still attach bash+task definitions and the subagent runner so Cursor exec
    // can invoke `task` the same way the local tool loop does.
    const bool is_server_duplex_agent =
        (client.tool_execution_model() == ToolExecutionModel::ServerSideDuplex);
    if (enable_tools) {
      const bool enable_task_tool = !is_subagent;
      const bool supports_vision = (resolved_model != nullptr && resolved_model->vision);
      // Prompt-cache prefix: same builder compaction uses to replay tools.
      base_opts.tools = build_turn_tools(enable_task_tool, supports_vision);
      int max_tool_steps = 0;
      if (const char* env_steps = std::getenv("QCODE_MAX_STEPS")) {
        try {
          const int v = std::stoi(env_steps);
          if (v >= 0) max_tool_steps = v;
        } catch (...) {}
      }
      base_opts.max_steps = max_tool_steps;

      if (enable_task_tool) {
        auto providers_ptr = std::make_shared<std::vector<ProviderInfo>>(providers);
        std::string current_prov_id = provider_id;
        std::string current_model_id = resolved_model_id;
        std::string current_workspace = ctx.workspace;
        std::shared_ptr<std::atomic<bool>> main_abort = ctx.abort_flag;

        base_opts.subagent_runner =
            [providers_ptr, current_prov_id, current_model_id, current_workspace, main_abort](
                const JsonValue& args,
                std::shared_ptr<std::atomic<bool>> task_abort) -> JsonValue {
              return run_subagent_turn_multi(
                  providers_ptr, current_prov_id, current_model_id,
                  current_workspace, main_abort, std::move(task_abort), args);
            };
        base_opts.routing_board = [providers_ptr, current_prov_id, current_model_id] {
          return routing::format_routing_table(*providers_ptr,
                                               qcode::session::load_subagent_stats(),
                                               current_prov_id, current_model_id,
                                               std::time(nullptr));
        };
      }
    }
    if (enable_tools && !is_server_duplex_agent) {
      auto refresh_client = [&](qcode::Client& out_client) -> bool {
        // Re-read Antigravity OAuth (force refresh on 401).
        if (provider_id.find("antigravity") != std::string::npos ||
            provider_name.find("Antigravity") != std::string::npos) {
          const auto fresh = get_antigravity_token(/*force_refresh=*/true);
          if (fresh.empty()) {
            LOG_ERROR("generation_service: Antigravity token refresh returned empty");
            return false;
          }
          provider_options.api_key = fresh;
        } else if (provider_id == "anthropic" || provider_name == "Anthropic") {
          const auto fresh = get_anthropic_token(/*force_refresh=*/true);
          if (fresh.empty()) {
            LOG_ERROR("generation_service: Anthropic token refresh returned empty");
            return false;
          }
          provider_options.api_key = fresh;
        }
        auto resolution =
            qcode::providers::ProviderRegistry::instance().resolve(
                provider_id, provider_options);
        if (!resolution.ok()) {
          LOG_ERROR("generation_service: credential refresh failed: {}", resolution.error);
          return false;
        }
        out_client = std::move(resolution.client);
        LOG_INFO("generation_service: refreshed provider credentials for {}", provider_id);
        return true;
      };
      run_tools_generation_bus(client, std::move(base_opts), bus, ctx,
                               refresh_client, provider_id);
    } else {
      if (is_server_duplex_agent) {
        LOG_INFO("ChatBus: ServerSideDuplex native agent stream (session_id={})",
                 ctx.session_id);
        bus.publish<SessionStatusChanged>({
            .session_id = ctx.session_id,
            .status = "agent",
        });
      }
      run_stream_generation_bus(client, std::move(base_opts), bus, ctx, provider_id);
    }

    // Nested helpers usually publish idle themselves; a second idle from the
    // guard is harmless and covers early-return / fatal paths that forget it.

  } catch (const std::exception& e) {
    std::string err_msg = e.what();
    bus.publish<ErrorOccurred>({
        .session_id = ctx.session_id,
        .message = "Exception: " + err_msg,
        .severity = "error"
    });
  }
}

// ════════════════════════════════════════════════════════════════════════════
//  GenerationService implementation
// ════════════════════════════════════════════════════════════════════════════

void qcode::GenerationService::run_generation(
    const std::string& provider_name,
    const std::string& model_id,
    const std::string& system_prompt,
    qcode::Messages messages,
    bool enable_tools,
    GenerationContext& ctx)
{
    qcode::run_generation_with_bus(provider_name, model_id, system_prompt,
                            std::move(messages), enable_tools, providers_, bus_, ctx);
}
} // namespace qcode
