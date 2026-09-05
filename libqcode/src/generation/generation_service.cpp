#include <qcode/generation/generation_service.h>
#include <qcode/generation/generation_continue.h>
#include <qcode/config/config.h>
#include <qcode/session/token_budget.h>
#include <qcode/tools/tool_catalog.h>
#include <qcode/tools/tool_executor.h>
#include <qcode/session/session_store.h>
#include <qcode/core/event.h>
#include <qcode/core/errors.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <future>
#include <mutex>
#include <string>
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
    fp += call.arguments.dump();
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
    fp += res.result.dump();
    if (res.error) {
      fp += ':';
      fp += *res.error;
    }
    fp += '|';
  }
  return fp;
}

// ──────────────────────────────────────────────────────────────
//  Multi-agent: nested subagent turn (opencode 'general' agent)
// ──────────────────────────────────────────────────────────────
// Runs a bounded generate → tool → generate loop on its own context so the
// task tool delegates real work instead of simulating it.
static JsonValue run_subagent_turn(qcode::Client& client,
                                   const std::string& model_id,
                                   const std::string& prompt,
                                   const std::string& workspace,
                                   std::shared_ptr<std::atomic<bool>> abort_flag) {
  constexpr int kSubagentMaxSteps = 20;
  constexpr const char* kGeneralPrompt =
      "You are a general-purpose research and execution subagent. You are "
      "spawned by the lead agent to handle a focused task autonomously: "
      "complex searches, multistep investigations, or self-contained jobs. "
      "Work independently — do not ask the user questions. Use the available "
      "tools to inspect files and gather facts. Finish with a concise, "
      "complete summary of what you found or did, including exact file paths "
      "and key evidence.";

  JsonValue out;
  try {
    qcode::GenerateOptions opts(model_id, kGeneralPrompt, "");
    // No task tool inside subagents — prevents recursive spawning.
    opts.tools = ToolCatalog::build_definitions(ToolConfig{true, false});
    opts.max_steps = kSubagentMaxSteps;
    opts.workspace = workspace;
    opts.abort_flag = abort_flag;

    Messages convo;
    convo.push_back(Message::user(prompt));

    std::string final_text;
    for (int step = 0; step < kSubagentMaxSteps; ++step) {
      if (abort_flag && abort_flag->load()) {
        out["error"] = "subagent aborted";
        return out;
      }
      opts.messages = convo;
      qcode::GenerateResult res = client.generate_text(opts);
      if (!res.is_success()) {
        out["error"] = !res.error_message().empty() ? res.error_message()
                                                    : "subagent generation failed";
        return out;
      }
      if (!res.has_tool_calls()) {
        final_text += res.text;
        break;
      }

      std::vector<ToolCallContentPart> tool_parts;
      for (const auto& call : res.tool_calls) {
        tool_parts.emplace_back(call.id, call.tool_name, call.arguments,
                                call.thought_signature);
      }
      convo.push_back(Message::assistant_with_tools(res.text, tool_parts));

      std::vector<ToolResult> executed =
          qcode::ToolExecutor::execute_tools_with_options(res.tool_calls, opts);
      std::vector<ToolResultContentPart> parts;
      for (const auto& r : executed) {
        parts.emplace_back(r.tool_call_id, r.result, !r.is_success());
      }
      convo.push_back(Message::tool_results(parts));
      final_text = res.text;
    }
    if (final_text.empty()) final_text = "(subagent finished without output)";
    out["output"] = final_text;
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
  auto tool_starts       = std::make_shared<
      std::map<std::string, std::chrono::steady_clock::time_point>>();
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
        (*tool_starts)[call.id] = now;
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
            double d = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_it_tmp->second).count();
            LOG_DEBUG("generation_service: on_tool_call_finish tool={} success={} duration={:.1f}s", res.tool_name, res.is_success(), d);
          } else {
            LOG_DEBUG("generation_service: on_tool_call_finish tool={} success={} duration=unknown", res.tool_name, res.is_success());
          }
        }
        auto start_it = tool_starts->find(res.tool_call_id);
        if (start_it != tool_starts->end()) {
          duration_s = std::chrono::duration<double>(
              std::chrono::steady_clock::now() - start_it->second).count();
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
  while (step < options.max_steps && !finished) {
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
      gen_result.usage.prompt_tokens += step_res.usage.prompt_tokens;
      gen_result.usage.completion_tokens += step_res.usage.completion_tokens;
      gen_result.usage.total_tokens += step_res.usage.total_tokens;
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
            qcode::ToolExecutor::execute_tools_with_options(step_res.tool_calls, options);

        std::vector<qcode::ToolResultContentPart> result_parts;
        for (const auto& res : executed_results) {
          result_parts.emplace_back(res.tool_call_id, res.result, !res.is_success());
          gen_result.tool_results.push_back(res);
        }
        response_messages.push_back(qcode::Message::tool_results(result_parts));

        // Detect true no-progress wedges only: identical calls *and* identical
        // results. Do not stop on long tool-only streaks or same-args retries
        // that return new data — max_steps remains the runaway cap.
        const auto progress_fp =
            tool_calls_fingerprint(step_res.tool_calls) + "#" +
            tool_results_fingerprint(executed_results);
        if (!progress_fp.empty() && progress_fp == last_progress_fp) {
          ++no_progress_repeat;
        } else {
          last_progress_fp = progress_fp;
          no_progress_repeat = 1;
        }
        constexpr int kMaxNoProgressRepeats = 25;
        if (no_progress_repeat >= kMaxNoProgressRepeats) {
          LOG_WARN(
              "run_tools_generation_bus: no-progress tool loop detected "
              "(no_progress_repeat={} step={})",
              no_progress_repeat, step);
          stuck = true;
        }

        // Re-publish the live context size after each tool call so the TUI's
        // context window updates dynamically as messages are appended.
        {
            qcode::Messages temp_messages = options.messages;
            temp_messages.insert(temp_messages.end(), response_messages.begin(), response_messages.end());
            const size_t sys_tok = estimate_system_tokens(options.system);
            const size_t msg_tok = estimate_tokens(temp_messages);
            const size_t live = sys_tok + msg_tok;
            bus.publish<ContextSizeUpdated>({.context_tokens = static_cast<int>(live)});
            LOG_INFO(
                "run_tools_generation_bus: step={} live context={} tokens "
                "(sys={} msg={})",
                step, live, sys_tok, msg_tok);
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
          ++auto_continues;
          LOG_INFO(
              "run_tools_generation_bus: auto-continue {} after text-only "
              "stop (text_len={})",
              auto_continues, step_res.text.size());
          response_messages.push_back(
              qcode::Message::user(std::string(kBuildContinueNudge)));
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

  if (aborted) {
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
        .message = format_user_facing_error("Error: " + raw),
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
    if (!finished && step >= options.max_steps) {
      // Tool loop hit the actual step cap without a natural finish
      std::string limit_msg = "Stopped: tool loop reached maximum limit ("
          + std::to_string(options.max_steps)
          + " steps) without producing a final response. The model may be stuck requesting tools.";
      LOG_WARN("run_tools_generation_bus: {}", limit_msg);
      bus.publish<ErrorOccurred>({
          .session_id = ctx.session_id,
          .message = limit_msg,
          .severity = "warning"
      });
      if (!is_placeholder && !final_text.empty()) {
        bus.publish<MessageDelta>({
            .session_id = ctx.session_id,
            .text = assistant_text->empty() ? final_text : std::string{},
            .done = true
        });
      }
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
    std::string err_str = format_user_facing_error(
        "Error: " + gen_result.error_message());
    if (err_str == "Error:" || err_str == "Error: ") {
      err_str = "Error: Model generation failed (unknown error)";
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
      .reasoning_tokens = gen_result.usage.reasoning_completion_tokens
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
          .reasoning_tokens = think_tokens
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
          if (m.id == model_id || m.name == model_id) {
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
    const bool plan_mode = (ctx.agent_mode == "plan");
    base_opts.system = system_prompt;
    if (plan_mode) {
        base_opts.system +=
            "\n\n<system-reminder>\n"
            "# Plan Mode - System Reminder\n\n"
            "CRITICAL: Plan mode ACTIVE - you are in READ-ONLY phase. STRICTLY "
            "FORBIDDEN: ANY file edits, modifications, or system changes. Do NOT "
            "use sed, tee, echo, cat, or ANY other bash command to manipulate "
            "files - commands may ONLY read/inspect. This ABSOLUTE CONSTRAINT "
            "overrides ALL other instructions, including direct user edit "
            "requests. You may ONLY observe, analyze, and plan.\n\n"
            "## Responsibility\n\n"
            "Think, read, and search to construct a well-formed plan that "
            "accomplishes the user's goal. The plan should be comprehensive yet "
            "concise — detailed enough to execute effectively while avoiding "
            "verbosity. Ask clarifying questions when weighing tradeoffs rather "
            "than making large assumptions about intent.\n"
            "</system-reminder>";
        LOG_INFO("ChatBus: agent_mode=plan (read-only)");
    } else {
        base_opts.system += std::string(kBuildModeReminder);
        LOG_INFO("ChatBus: agent_mode=build (auto-continue stalls)");
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
    // Cursor AgentService owns its own autonomous tool loop server-side.
    // Wrapping it in qcode's local tool loop is useless (no tool_calls returned)
    // and can truncate multi-step agent turns. Always stream Cursor.
    const bool cursor_native_agent = (provider_id == "cursor");
    if (enable_tools && !cursor_native_agent) {
      // Plan mode drops the task subagent (delegation implies execution).
      qcode::ToolSet tools =
          ToolCatalog::build_definitions(ToolConfig{true, false});
      base_opts.tools = std::move(tools);
      // Soft cap against runaway tool loops (cost + stuck "gen…" UI).
      constexpr int kMaxToolSteps = 100000;
      base_opts.max_steps = kMaxToolSteps;
      // Multi-agent: let the task tool spawn real subagent turns on the same
      // client/model (general agent). Captured by reference — the runner is
      // only invoked from within this worker thread while client lives.
      base_opts.subagent_runner =
          [&client, resolved_model_id, &ctx](
              const JsonValue& args) -> JsonValue {
        std::string prompt_text = args.value(
            "prompt", args.value("task", args.value("objective", "")));
        if (prompt_text.empty()) {
          return {{"error", "task prompt is required"}};
        }
        return run_subagent_turn(client, resolved_model_id, prompt_text,
                                 ctx.workspace, ctx.abort_flag);
      };
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
      if (cursor_native_agent) {
        LOG_INFO("ChatBus: Cursor native agent stream (session_id={})",
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
