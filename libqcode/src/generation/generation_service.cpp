#include <qcode/generation/turn_prefix.h>
#include <qcode/generation/generation_service.h>
#include <qcode/generation/generation_continue.h>
#include <qcode/config/config.h>
#include <qcode/session/token_budget.h>
#include <qcode/tools/tool_catalog.h>
#include <qcode/tools/task_target.h>
#include <qcode/tools/tool_executor.h>
#include <qcode/tools/multi_step_coordinator.h>
#include <qcode/session/session_store.h>
#include <qcode/core/event.h>
#include <qcode/core/errors.h>

#include <algorithm>
#include <atomic>
#include <random>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <string>
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

static std::string tool_calls_fingerprint(
    const std::vector<qcode::ToolCall>& calls) {
  std::string fp;
  for (const auto& call : calls) {
    fp += call.tool_name;
    fp += ':';
    fp += call.arguments.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    fp += '|';
  }
  return fp;
}

// Fingerprint of tool results so "same call, new output" is not treated as a
// stuck loop (e.g. re-read after edit, poll/retry bash).
static std::string tool_results_fingerprint(
    const std::vector<qcode::ToolResult>& results) {
  std::string fp;
  for (const auto& res : results) {
    fp += res.tool_name;
    fp += ':';
    fp += res.is_success() ? "ok:" : "err:";
    fp += res.result.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    if (res.error) {
      fp += ':';
      fp += *res.error;
    }
    fp += '|';
  }
  return fp;
}

// ──────────────────────────────────────────────────────────────
//  Multi-agent: nested subagent turn (parallel multi-provider & multi-model)
// ──────────────────────────────────────────────────────────────
namespace {
// P0.2: random-model fallback across working providers (zen + antigravity
// preferred, openrouter shuffled best-effort). Cursor only when explicit.
constexpr int kSubagentFallbackMaxAttempts = 4;

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

bool subagent_failover_worthy(const std::string& msg, const GenerateResult& res) {
  if (res.is_retryable.value_or(false)) return true;
  if (msg.find("ModelError") != std::string::npos) return true;
  if (msg.find("FreeTierError") != std::string::npos) return true;
  if (msg.find("AuthError") != std::string::npos) return true;
  if (msg.find("Missing API key") != std::string::npos) return true;
  if (msg.find("free tier can only be used") != std::string::npos) return true;
  if (msg.find("is not supported") != std::string::npos) return true;
  if (msg.find("Failed to resolve subagent client") != std::string::npos) return true;
  if (msg.find("subagent generation failed") != std::string::npos) return true;
  if (msg.find("401") != std::string::npos || msg.find("403") != std::string::npos ||
      msg.find("404") != std::string::npos || msg.find("429") != std::string::npos ||
      msg.find("500") != std::string::npos || msg.find("502") != std::string::npos ||
      msg.find("503") != std::string::npos || msg.find("504") != std::string::npos) return true;
  return is_error_message_retryable(msg);
}
}  // namespace

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

  SubagentTarget target;
  if (providers) {
    target = resolve_subagent_target(args, *providers, default_provider_id,
                                     default_model_id);
  } else {
    target.error = "No available AI provider configured for subagent";
  }
  if (!target.error.empty() && !target.provider) {
    return JsonValue{{"error", target.error}};
  }

  const ProviderInfo* target_provider = target.provider;
  std::string target_model_id = target.model_id;
  const ModelInfo* target_model_info = target.model_info;

  if (!target_provider) {
    return JsonValue{{"error", "No available AI provider configured for subagent"}};
  }

  // P0.2 fallback chain: explicit target, then working-set pool (zen,
  // antigravity preferred; openrouter tier shuffled; cursor only if explicit).
  struct FallbackCand { const ProviderInfo* p; const ModelInfo* m; std::string model_id; };
  std::vector<FallbackCand> chain;
  chain.push_back({target_provider, target_model_info, target_model_id});
  {
    const bool allow_cursor = subagent_wants_cursor(args);
    std::vector<FallbackCand> rest;
    for (const auto& pr : *providers) {
      if (!is_provider_authenticated(pr)) continue;
      const bool is_cursor = pr.id == "cursor" || pr.id.find("cursor") != std::string::npos;
      if (is_cursor && !allow_cursor) continue;
      for (const auto& mo : pr.models) {
        if (mo.id.empty()) continue;
        if (!is_model_working(pr, mo)) continue;
        if (pr.id == target_provider->id && mo.id == target_model_id) continue;
        if (matches_orchestrator(pr.id, mo.id, default_provider_id, default_model_id)) continue;
        rest.push_back({&pr, &mo, mo.id});
      }
    }
    // OpenCode models configured in opencode.json are supported in the rest pool.

    auto prio = [](const ProviderInfo* q) {
      if (q->id.find("antigravity") != std::string::npos) return 0;
      if (q->id == "openrouter") return 1;
      if (q->id == "opencode") return 2;
      return 3;
    };
    std::stable_sort(rest.begin(), rest.end(),
                     [&](const FallbackCand& a, const FallbackCand& b) { return prio(a.p) < prio(b.p); });
    auto oit = std::find_if(rest.begin(), rest.end(),
                            [](const FallbackCand& c) { return c.p->id == "openrouter"; });
    if (oit != rest.end()) {
      std::random_device rd;
      std::mt19937 g(rd());
      std::shuffle(oit, rest.end(), g);
    }
    for (auto& c : rest) chain.push_back(c);
  }
  const int max_attempts = std::min<int>(kSubagentFallbackMaxAttempts, (int)chain.size());

  JsonValue out;
  try {
  std::string fb_last_error;
  for (int fb_attempt = 0; fb_attempt < max_attempts; ++fb_attempt) {
    const auto& fb = chain[(size_t)fb_attempt];
    target_provider = fb.p;
    target_model_info = fb.m;
    target_model_id = fb.model_id;
    if (fb_attempt > 0) {
      LOG_WARN("subagent fallback attempt {}/{}: {}:{} (prev: {})", fb_attempt + 1, max_attempts,
               target_provider->id, target_model_id, fb_last_error);
    }

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
        if (key && *key != '\0') prov_opts.api_key = key;
      }
    }

    const auto call = prepare_provider_call(prov_opts, target_provider->id, target_model_id, "");
    std::string wire_model = call.wire_model_id.empty() ? target_model_id : call.wire_model_id;

    auto resolution = qcode::providers::ProviderRegistry::instance().resolve(
        target_provider->id, prov_opts);
    if (!resolution.ok()) {
      fb_last_error = "Failed to resolve subagent client for provider '" +
                      target_provider->id + "': " + resolution.error;
      LOG_WARN("subagent fallback: {}", fb_last_error);
      continue;
    }
    qcode::Client subagent_client = std::move(resolution.client);

    std::string sub_session_id =
        args.value("sessionId", args.value("session_id", args.value("task_id", "")));
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

    if (aborted()) {
      out["error"] = "Subagent cancelled due to abort flag";
      return out;
    }
    if (!res.is_success()) {
      const std::string emsg = !res.error_message().empty() ? res.error_message()
                                                            : "subagent generation failed";
      if (fb_attempt + 1 < max_attempts && subagent_failover_worthy(emsg, res)) {
        fb_last_error = emsg;
        LOG_WARN("subagent attempt {}/{} failed on {}:{}: {}", fb_attempt + 1, max_attempts,
                 target_provider->id, target_model_id, emsg);
        continue;
      }
      out["error"] = emsg;
      return out;
    }

    std::string final_text = res.text;
    if (final_text.empty()) final_text = "(subagent finished without output)";
    out["output"] = final_text;
    if (fb_attempt > 0) {
      out["fallback_used"] = true;
      out["fallback_attempts"] = fb_attempt + 1;
      out["fallback_model"] = target_provider->id + ":" + target_model_id;
    }
    return out;
  }  // end fallback loop
  out["error"] = fb_last_error.empty() ? "subagent generation failed" : fb_last_error;
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
    qcode::GenerateOptions options,
    bus::BusPort& bus,
    GenerationContext& ctx,
    const std::function<bool(qcode::Client&)>& refresh_client = nullptr,
    const std::string& provider_id = "") {
  auto assistant_text    = std::make_shared<std::string>();
  auto assistant_msg_idx = std::make_shared<int>(-1);
  struct InFlightTool {
    std::chrono::steady_clock::time_point started;
    std::string tool_name;
  };
  auto tool_starts = std::make_shared<std::map<std::string, InFlightTool>>();
  auto callback_mutex    = std::make_shared<std::mutex>();
  auto step_counter      = std::make_shared<std::atomic<int>>(0);
  int  max_steps         = options.max_steps;

  // ── Step finished: emit MessageDelta ──
  options.on_step_finish =
      [&bus, &ctx, assistant_text,
       callback_mutex](const qcode::GenerateStep& step) {
        std::lock_guard<std::mutex> lock(*callback_mutex);
        LOG_DEBUG("generation_service: on_step_finish text_len={}", step.text.size());
        if (step.text.empty()) return;
        if (*assistant_text == "  \u23f3 Working...") {
          *assistant_text = step.text;
        } else {
          *assistant_text += step.text;
        }
        if (step.text == "  \u23f3 Working...") return;
        bus.publish<MessageDelta>({
            .session_id = ctx.session_id,
            .text = step.text,
            .done = false
        });
      };

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
      [&bus, &ctx, assistant_text, tool_starts,
       callback_mutex](const qcode::ToolResult& res) {
        std::lock_guard<std::mutex> lock(*callback_mutex);
        double duration_s = 0.0;
        {
          auto start_it_tmp = tool_starts->find(res.tool_call_id);
          if (start_it_tmp != tool_starts->end()) {
            double d = std::chrono::duration<double>(
                           std::chrono::steady_clock::now() -
                           start_it_tmp->second.started)
                           .count();
            LOG_DEBUG("generation_service: on_tool_call_finish tool={} success={} duration={:.1f}s", res.tool_name, res.is_success(), d);
          } else {
            LOG_DEBUG("generation_service: on_tool_call_finish tool={} success={} duration=unknown", res.tool_name, res.is_success());
          }
        }
        auto start_it = tool_starts->find(res.tool_call_id);
        if (start_it != tool_starts->end()) {
          duration_s = std::chrono::duration<double>(
              std::chrono::steady_clock::now() - start_it->second.started).count();
          tool_starts->erase(start_it);
        }

        if (*assistant_text == "  \u23f3 Working...") {
          *assistant_text = "";
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

  // ── Multi-step generation loop ──
  qcode::GenerateResult gen_result;
  gen_result.finish_reason = qcode::kFinishReasonStop;

  qcode::Messages response_messages;
  qcode::Messages step_messages = options.messages;
  if (step_messages.empty() && !options.prompt.empty()) {
    step_messages.push_back(qcode::Message::user(options.prompt));
  }
  const size_t initial_count = step_messages.size();

  qcode::GenerateOptions step_opts = options;
  step_opts.prompt.clear();

  int step = 0;
  bool finished = false;
  bool aborted = false;
  bool stuck = false;
  bool auth_retried = false;
  int auto_continues = 0;
  const bool plan_mode = (ctx.agent_mode == "plan");
  // Consecutive steps with the same tool calls AND the same results.
  // Same-args retries with changing output (re-read, poll) are allowed.
  int no_progress_repeat = 0;
  std::string last_progress_fp;

  LOG_DEBUG("run_tools_generation_bus: starting loop max_steps={}", options.max_steps);
  while (!finished && (options.max_steps <= 0 || step < options.max_steps)) {
    if (ctx.abort_flag && ctx.abort_flag->load()) {
      LOG_INFO("run_tools_generation_bus: abort requested at step={}", step);
      aborted = true;
      break;
    }
    step_messages.erase(std::next(step_messages.begin(), initial_count), step_messages.end());
    step_messages.insert(step_messages.end(), response_messages.begin(), response_messages.end());
    step_opts.messages = step_messages;
    LOG_DEBUG("run_tools_generation_bus: step={} messages={}", step, step_messages.size());
    step_opts.max_steps = 1;
    step_opts.on_retry = [&bus, &ctx](int attempt, int total_attempts,
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

    {
      LOG_DEBUG("run_tools_generation_bus: step={} sync generate_text", step);
      // generate_text is a blocking HTTP POST. Without a heartbeat the TUI
      // sits on "generating" for the full read timeout and looks wedged.
      auto fut = std::async(std::launch::async, [&client, step_opts]() {
        return client.generate_text(step_opts);
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
      qcode::GenerateResult step_res = fut.get();
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
        if (!auth_retried && looks_like_auth_error(err) && refresh_client &&
            refresh_client(client)) {
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
        gen_result.finish_reason = qcode::kFinishReasonError;
        gen_result.provider_metadata = step_res.provider_metadata;
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
      if (!step_res.reasoning.empty()) {
        bus.publish<ReasoningDelta>({
            .session_id = ctx.session_id,
            .text = step_res.reasoning,
            .signature = "",
            .done = true,
        });
      }

      gen_result.text += step_res.text;
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
      // Thinking-token accounting: same per-turn-absolute semantics.
      gen_result.usage.reasoning_completion_tokens =
          std::max(gen_result.usage.reasoning_completion_tokens,
                   step_res.usage.reasoning_completion_tokens);
      if (gen_result.usage.reasoning_completion_tokens == 0 &&
          !step_res.reasoning.empty()) {
        gen_result.usage.reasoning_completion_tokens = std::max(
            1, static_cast<int>(step_res.reasoning.size() / 4));
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
      gen_result.finish_reason = step_res.finish_reason;
      gen_result.id = step_res.id;
      gen_result.model = step_res.model;
      gen_result.created = step_res.created;
      gen_result.system_fingerprint = step_res.system_fingerprint;

      LOG_DEBUG("run_tools_generation_bus: step={} has_tool_calls={} text_len={}", step, step_res.has_tool_calls(), step_res.text.size());
      if (step_res.has_tool_calls()) {
        std::vector<qcode::ToolCallContentPart> tool_parts;
        for (const auto& call : step_res.tool_calls) {
          tool_parts.emplace_back(call.id, call.tool_name, call.arguments,
                                  call.thought_signature);
          gen_result.tool_calls.push_back(call);
        }
        if (!step_res.response_messages.empty()) {
          // Parser already attached the reasoning part alongside tool calls —
          // reuse it so thinking replays across steps.
          response_messages.push_back(step_res.response_messages.front());
        } else {
          response_messages.push_back(
              qcode::Message::assistant_with_tools(step_res.text, tool_parts));
        }

        // Execute tool calls to produce tool results
        std::vector<qcode::ToolResult> executed_results =
            qcode::ToolExecutor::execute_tools_with_options(step_res.tool_calls, options, /*parallel=*/true);

        std::vector<qcode::ToolResultContentPart> result_parts;
        for (const auto& res : executed_results) {
          result_parts.emplace_back(res.tool_call_id, res.result, !res.is_success());
          gen_result.tool_results.push_back(res);
        }
        response_messages.push_back(qcode::Message::tool_results(result_parts));

        // Detect true no-progress wedges only: identical calls *and* identical
        // results. Do not stop on long tool-only streaks or same-args retries
        // that return new data. Uncapped: instead of halting, inject a
        // corrective nudge so the model breaks the repetition itself.
        const auto progress_fp =
            tool_calls_fingerprint(step_res.tool_calls) + "#" +
            tool_results_fingerprint(executed_results);
        if (!progress_fp.empty() && progress_fp == last_progress_fp) {
          ++no_progress_repeat;
        } else {
          last_progress_fp = progress_fp;
          no_progress_repeat = 1;
        }
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
          response_messages.push_back(qcode::Message::user(
              "[System Note: Your previous tool call produced the exact same result. "
              "Do not repeat the identical call or command; change parameters, "
              "try a different approach, inspect a different file, or conclude if finished.]"));
        }

        // Re-publish the live context size after each tool call so the TUI's
        // context window updates dynamically as messages are appended.
        {
            size_t live = 0;
            if (step_res.usage.prompt_tokens > 0) {
                const size_t tool_res_tok = estimate_tokens(
                    qcode::Messages{qcode::Message::tool_results(result_parts)});
                live = step_res.usage.prompt_tokens + tool_res_tok;
            } else {
                qcode::Messages temp_messages = options.messages;
                temp_messages.insert(temp_messages.end(), response_messages.begin(), response_messages.end());
                live = estimate_system_tokens(options.system) + estimate_tokens(temp_messages);
            }
            bus.publish<ContextSizeUpdated>({.context_tokens = static_cast<int>(live)});
            LOG_INFO("run_tools_generation_bus: step={} live context={} tokens", step, live);
        }

        if (options.on_step_finish) {
          qcode::GenerateStep step_data;
          step_data.text = step_res.text;
          step_data.tool_calls = step_res.tool_calls;
          step_data.tool_results = step_res.tool_results;
          step_data.finish_reason = step_res.finish_reason;
          step_data.usage = step_res.usage;
          options.on_step_finish.value()(step_data);
        }
        if (stuck) break;
        if (ctx.has_queued_work && ctx.has_queued_work()) {
          LOG_INFO(
              "run_tools_generation_bus: queued prompt pending after tool step={}; "
              "yielding turn to pick it up immediately",
              step);
          finished = true;
          break;
        }
      } else {
        no_progress_repeat = 0;
        last_progress_fp.clear();
        response_messages.push_back(qcode::Message::assistant(step_res.text));
        if (options.on_step_finish) {
          qcode::GenerateStep step_data;
          step_data.text = step_res.text;
          step_data.finish_reason = step_res.finish_reason;
          step_data.usage = step_res.usage;
          options.on_step_finish.value()(step_data);
        }
        if (should_auto_continue_build(plan_mode, auto_continues,
                                       step_res.text)) {
          if (ctx.has_queued_work && ctx.has_queued_work()) {
            LOG_INFO(
                "run_tools_generation_bus: queued prompt pending; skipping "
                "auto-continue to pick up queued prompt immediately");
            finished = true;
          } else {
            ++auto_continues;
            LOG_INFO(
                "run_tools_generation_bus: auto-continue {} after text-only "
                "stop (text_len={})",
                auto_continues, step_res.text.size());
            response_messages.push_back(
                qcode::Message::user(std::string(kBuildContinueNudge)));
          }
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
           step, gen_result.text.size(), gen_result.tool_calls.size(), aborted, stuck);
  gen_result.response_messages = response_messages;

  // If we reached the tool step limit without producing any text, synthesize a
  // final textual summary of the findings with tools disabled so the turn completes
  // naturally instead of leaving the user with an empty error.
  const bool capped = (options.max_steps > 0);
  if (!aborted && !stuck && !finished && capped && step >= options.max_steps &&
      (assistant_text->empty() || *assistant_text == "  \u23f3 Working...") &&
      (gen_result.text.empty() || gen_result.text == "  \u23f3 Working...")) {
    LOG_INFO("run_tools_generation_bus: step cap reached ({} steps); synthesizing final response without tools",
             options.max_steps);
    qcode::GenerateOptions synth_opts = options;
    synth_opts.tools.clear();
    synth_opts.max_steps = 1;
    qcode::Messages synth_messages = options.messages;
    synth_messages.insert(synth_messages.end(), response_messages.begin(), response_messages.end());
    synth_messages.push_back(qcode::Message::user(
        "[System Note: You have reached the maximum tool steps for this turn. Please summarize your progress, key findings, what changes were made, and your next recommended steps directly to the user now without requesting any further tools.]"
    ));
    synth_opts.messages = std::move(synth_messages);

    auto synth_res = client.generate_text(synth_opts);
    if (synth_res.is_success() && !synth_res.text.empty()) {
      finished = true;
      gen_result.text = synth_res.text;
      *assistant_text = synth_res.text;
      gen_result.usage.prompt_tokens = synth_res.usage.prompt_tokens;
      gen_result.usage.completion_tokens += synth_res.usage.completion_tokens;
      gen_result.usage.total_tokens =
          gen_result.usage.prompt_tokens + gen_result.usage.completion_tokens;
      response_messages.push_back(qcode::Message::assistant(synth_res.text));
      gen_result.response_messages = response_messages;
      bus.publish<MessageDelta>({
          .session_id = ctx.session_id,
          .text = synth_res.text,
          .done = false
      });
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

  bool fatal_error = false;
  const bool has_error = gen_result.error.has_value() && !gen_result.error->empty();

  if (has_error) {
    fatal_error = true;
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
  } else if (gen_result.is_success()) {
    std::string final_text;
    if (!assistant_text->empty()) final_text = *assistant_text;
    else final_text = gen_result.text;

    LOG_DEBUG("run_tools_generation_bus: final assistant_text empty={} gen_result.text empty={}",
             assistant_text->empty(), gen_result.text.empty());
    bool is_placeholder = (final_text == "  \u23f3 Working...");
    if (!finished && capped && step >= options.max_steps) {
      if (final_text.empty() || is_placeholder) {
        final_text = "I reached the tool execution limit (" + std::to_string(options.max_steps) +
                     " steps) for this turn while working on your request. Send 'continue' to proceed with the next steps.";
        if (assistant_text) *assistant_text = final_text;
        gen_result.text = final_text;
        is_placeholder = false;
        bus.publish<MessageDelta>({
            .session_id = ctx.session_id,
            .text = final_text,
            .done = false
        });
      }
      bus.publish<MessageDelta>({
          .session_id = ctx.session_id,
          .text = "",
          .done = true
      });
    } else if (!final_text.empty() && !is_placeholder) {
      LOG_DEBUG("run_tools_generation_bus: publishing final MessageDelta text_len={}", final_text.size());
      bus.publish<MessageDelta>({
          .session_id = ctx.session_id,
          .text = assistant_text->empty() ? final_text : std::string{},
          .done = true
      });
    } else if (!final_text.empty() && is_placeholder) {
      // Model left only the in-progress placeholder; treat as no real text.
      LOG_WARN("run_tools_generation_bus: model returned only the in-progress placeholder");
      bus.publish<ErrorOccurred>({
          .session_id = ctx.session_id,
          .message = "The model returned an empty response (no text generated).",
          .severity = "warning"
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

  } else {
    fatal_error = true;
    std::string err_str = format_user_facing_error(gen_result.error_message());
    while (err_str.starts_with("Error: Error")) {
      err_str.erase(0, 7);
    }
    if (err_str.empty() || err_str == "Error" || err_str == "Error:" || err_str == "Error: Error") {
      err_str = "Error: Model generation failed (unknown error)";
    } else if (!err_str.starts_with("Error:") && !err_str.starts_with("Exception:")) {
      err_str = "Error: " + err_str;
    }
    if (gen_result.provider_metadata.has_value() &&
        !gen_result.provider_metadata->empty()) {
      LOG_ERROR("run_tools_generation_bus: provider_metadata {}",
                gen_result.provider_metadata->substr(0, 500));
    }
    bus.publish<ErrorOccurred>({
        .session_id = ctx.session_id,
        .message = err_str,
        .severity = "error"
    });
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
      text_buffer += event.text_delta;
      text_size += event.text_delta.size();
      LOG_DEBUG("run_stream_generation_bus: text_delta buffer_size={}", text_buffer.size());
      auto now = std::chrono::steady_clock::now();
      if (now - last_text_flush >= kFlushInterval) {
        flush_text();
        last_text_flush = now;
      }
    } else if (event.is_reasoning_delta()) {
      // OpenRouter encrypts reasoning as [REDACTED]; drop those chunks.
      std::string chunk = event.text_delta;
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
    const qcode::Messages& messages,
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

    // ── Agent mode (mirrors opencode build/plan) ──
    // Plan mode appends the read-only research contract from upstream's
    // plan.txt and drops the task subagent tool.
    const bool is_subagent = (ctx.agent_mode == "subagent") ||
                             (!ctx.session_id.empty() && qcode::session::is_child_session(ctx.session_id));
    const bool plan_mode = (ctx.agent_mode == "plan");
    // Prompt-cache prefix: single source of truth shared with compaction
    // (build_cache_replay_request) so both requests stay byte-identical.
    base_opts.system = build_turn_system_prompt(system_prompt, plan_mode,
                                                 is_subagent, providers);
    if (plan_mode) {
        LOG_INFO("ChatBus: agent_mode=plan (read-only)");
    } else if (is_subagent) {
        LOG_INFO("ChatBus: agent_mode=subagent (focused worker, nested delegation disabled)");
    } else {
        LOG_INFO("ChatBus: agent_mode=orchestrator (lead coordinator with parallel subagents)");
    }

    Model transform_model(resolved_model_id, provider_id);
    base_opts.messages = ProviderTransform::normalize_messages(messages, transform_model);
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
    // Empty /variant = auto default for every reasoning model. Explicit "off"
    // disables thinking. Native Anthropic uses budget_tokens; every other
    // provider (OpenAI, OpenRouter, Antigravity/Gemini, Cursor, Zen) uses
    // reasoning_effort, including Claude ids that ride those transports.
    const std::string& rm = ctx.reasoning_mode;
    const auto effort_budget = [](const std::string& effort) {
      return effort == "low"      ? 2000
             : effort == "medium" ? 8000
             : effort == "max"    ? 24000
                                  : 16000;
    };
    if (rm != "off") {
      std::string effort;
      if (rm.empty()) {
        if (resolved_model && resolved_model->reasoning) {
          effort = ProviderTransform::default_variant(*resolved_model);
        }
      } else if (resolved_model) {
        effort = ProviderTransform::clamp_variant(*resolved_model, rm);
      } else {
        effort = rm;
      }
      if (!effort.empty() && effort != "off") {
        base_opts.reasoning_effort = effort;
        if (provider_id.find("anthropic") != std::string::npos) {
          base_opts.budget_tokens = effort_budget(effort);
        }
        if (!rm.empty() && effort != rm) {
          LOG_INFO("generation_service: clamped variant '{}' -> '{}' for {}",
                   rm, effort, resolved_model_id);
        } else if (rm.empty()) {
          LOG_INFO("generation_service: default thinking variant '{}' for {}",
                   effort, resolved_model_id);
        }
      }
    }

    // ── Dispatch ──
    // ServerSideDuplex providers (e.g. Cursor AgentService) own their autonomous tool loop.
    // Wrapping them in qcode's local tool loop is useless (no tool_calls returned)
    // and can truncate multi-step agent turns. Always stream ServerSideDuplex providers.
    // Still attach bash+task definitions and the subagent runner so Cursor exec
    // can invoke `task` the same way the local tool loop does.
    const bool is_server_duplex_agent =
        (client.tool_execution_model() == ToolExecutionModel::ServerSideDuplex);
    if (enable_tools) {
      const bool enable_task_tool = (!plan_mode && !is_subagent);
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
    const qcode::Messages& messages,
    bool enable_tools,
    GenerationContext& ctx)
{
    qcode::run_generation_with_bus(provider_name, model_id, system_prompt,
                            messages, enable_tools, providers_, bus_, ctx);
}
} // namespace qcode
