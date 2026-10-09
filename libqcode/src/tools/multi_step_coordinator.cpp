#include <cstdint>
#include <cstdlib>
#include <map>
#include <string_view>

#include <qcode/compaction/compaction_request.h>
#include <qcode/core/logger.h>
#include <qcode/session/token_budget.h>
#include <qcode/tools/multi_step_coordinator.h>
#include <qcode/session/session_store.h>
#include <qcode/tools/task_tool.h>
#include <qcode/tools/tool_executor.h>
#include <qcode/core/enums.h>
#include <qcode/core/tool.h>

namespace qcode {

namespace {

// FNV-1a fingerprint of a step's progress signal: ordered tool names,
// arguments, and results. Deliberately excludes free text (models vary
// chatter) and tool call ids (providers may mint unique ids per response),
// so identical fingerprints mean "same request, same result" — no progress.
constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

uint64_t fingerprint_step(const GenerateResult& r) {
  uint64_t h = kFnvOffset;
  auto mix = [&h](std::string_view s) {
    for (unsigned char c : s) {
      h ^= c;
      h *= kFnvPrime;
    }
  };
  for (const auto& tc : r.tool_calls) {
    mix(tc.tool_name);
    mix(tc.arguments.dump());
  }
  for (const auto& tr : r.tool_results) {
    mix(tr.is_success() ? tr.result.dump() : tr.error_message());
  }
  return h;
}

}  // namespace

GenerateResult MultiStepCoordinator::execute_multi_step(
    const GenerateOptions& initial_options,
    const std::function<GenerateResult(const GenerateOptions&)>&
        generate_func) {
  return execute_multi_step(initial_options, generate_func, AutoCompact{});
}

GenerateResult MultiStepCoordinator::execute_multi_step(
    const GenerateOptions& initial_options,
    const std::function<GenerateResult(const GenerateOptions&)>&
        generate_func,
    const AutoCompact& auto_compact) {
  if (initial_options.max_steps == 1) {
    // Single step - just execute normally.
    // max_steps <= 0 means uncapped: keep looping on tool calls until the
    // provider stops requesting them (user abort / provider error still stop).
    return generate_func(initial_options);
  }
  // max_steps <= 0 used to mean "uncapped", which let a pathological tool
  // loop run forever (default max_steps is 0). Resolve an effective safety
  // cap: an explicit positive max_steps wins, then QCODE_MAX_STEPS; only an
  // explicit QCODE_MAX_STEPS=0 keeps truly-uncapped behavior. Otherwise a
  // hard default guarantees the loop terminates.
  constexpr int kDefaultStepSafetyCap = 100;
  int effective_max_steps = initial_options.max_steps;
  bool env_steps_set = false;
  if (effective_max_steps <= 0) {
    if (const char* env_steps = std::getenv("QCODE_MAX_STEPS")) {
      try {
        const int v = std::stoi(env_steps);
        if (v >= 0) {
          effective_max_steps = v;
          env_steps_set = true;
        }
      } catch (...) {}
    }
  }
  const int step_cap = (effective_max_steps > 0)
                           ? effective_max_steps
                           : (env_steps_set ? -1 : kDefaultStepSafetyCap);

  // initial_messages is the user's original input, kept immutable across
  // steps; response_messages accumulates assistant turns and tool-result
  // turns. Each step's input is [initial_messages..., response_messages...].
  // system and other top-level options stay on initial_options.
  Messages initial_messages = initial_options.messages;
  if (initial_messages.empty() && !initial_options.prompt.empty()) {
    initial_messages.push_back(Message::user(initial_options.prompt));
  }
  size_t initial_count = initial_messages.size();
  Messages response_messages;

  // step_messages is grown in place each iteration (truncate to the immutable
  // prefix, then append response_messages) so we don't re-copy
  // initial_messages every step.
  Messages step_messages = std::move(initial_messages);
  GenerateOptions step_options = initial_options;
  step_options.prompt.clear();

  GenerateResult final_result;

  // Tracks whether the loop exited via the step cap with tool results still
  // unsummarized (set true only when a body completes with pending tool
  // results, cleared at the top of every iteration so any mid-body break
  // leaves it false).
  bool pending_tool_results = false;
  uint64_t prev_step_fp = 0;
  int fp_streak = 0;
  // 5 consecutive identical steps (each is a full LLM roundtrip) is an
  // unambiguous loop; high enough that slow legitimate polling (identical
  // status output a few times in a row) never trips it.
  constexpr int kMaxIdenticalSteps = 5;

  // Context of the next request (auto-compaction): the last step's reported
  // prompt tokens plus estimates of what it appended.
  size_t live_context = 0;
  size_t compact_threshold = auto_compact.threshold;
  int last_compact_step = -1;

  for (int step = 0; step_cap < 0 || step < step_cap; ++step) {
    pending_tool_results = false;

    // User abort: stop stepping immediately. The loop previously ignored
    // abort_flag entirely, so killed subagents kept calling the LLM/tools.
    // "Aborted by user" is the sentinel generation_service.cpp recognizes.
    if (initial_options.abort_flag && initial_options.abort_flag->load()) {
      final_result.finish_reason = kFinishReasonError;
      final_result.error = "Aborted by user";
      break;
    }

    // The subagent's inbox (messages from the lead or its sisters, reports of
    // background tasks it started) joins the conversation before each request.
    if (!initial_options.session_id.empty()) {
      for (auto& note : TaskTool::take_notices(initial_options.session_id)) {
        qcode::session::save_message(initial_options.session_id, "User", note);
        response_messages.push_back(Message::user(std::move(note)));
      }
    }

    // Truncate to the immutable prefix and re-append the running accumulator.
    // (vector::resize would require Message to be default-constructible.)
    step_messages.erase(std::next(step_messages.begin(), initial_count),
                        step_messages.end());
    step_messages.insert(step_messages.end(), response_messages.begin(),
                         response_messages.end());
    step_options.messages = step_messages;

    if (compact_threshold > 0 && live_context >= compact_threshold &&
        step_options.messages.size() > 2 &&
        (last_compact_step < 0 || step - last_compact_step >= 2)) {
      last_compact_step = step;
      LOG_INFO("MultiStepCoordinator: auto-compaction at {} tokens (threshold {}, "
               "messages={})", live_context, compact_threshold,
               step_options.messages.size());
      GenerateResult summary =
          generate_func(compaction::build_in_loop_request(step_options));
      if (summary.is_success() &&
          summary.text.find_first_not_of(" \t\r\n") != std::string::npos) {
        // Handoff file only for a stored session (it lives in its workspace).
        const std::string handoff =
            initial_options.session_id.empty()
                ? std::string()
                : compaction::write_handoff(initial_options.session_id, summary.text);
        const std::string body = compaction::continuation_message(
            summary.text, handoff, initial_options.workspace, /*mid_turn=*/true);
        step_messages.clear();
        step_messages.push_back(Message::user(body));
        initial_count = 1;
        response_messages.clear();
        step_options.messages = step_messages;
        live_context = estimate_tokens(step_messages);
        if (auto_compact.on_compacted) auto_compact.on_compacted(body);
      } else {
        LOG_WARN("MultiStepCoordinator: auto-compaction failed ({}); continuing",
                 summary.error_message());
        compact_threshold = 0;
      }
    }

    LOG_DEBUG("Executing step {} of {}, messages={}", step + 1,
                          (step_cap < 0 ? -1 : step_cap),
                          step_options.messages.size());

    GenerateResult step_result = generate_func(step_options);

    LOG_DEBUG(
        "Step {} result - text: '{}', tool_calls: {}, finish_reason: {}",
        step + 1, step_result.text, step_result.tool_calls.size(),
        static_cast<int>(step_result.finish_reason));

    if (!step_result.is_success()) {
      final_result.finish_reason = step_result.finish_reason;
      final_result.error = step_result.error;
      pending_tool_results = false;
      if (step == 0) {
        return step_result;
      }
      break;
    }

    // Execute tool calls if the model requested tools and they haven't been executed yet
    if (step_result.has_tool_calls() && step_result.tool_results.empty() && initial_options.has_tools()) {
      step_result.tool_results =
          ToolExecutor::execute_tools_with_options(step_result.tool_calls, initial_options, /*parallel=*/true);
    }

    // Record the per-step view exposed via `final_result.steps` and the
    // `on_step_finish` callback.
    GenerateStep generate_step;
    generate_step.text = step_result.text;
    generate_step.tool_calls = step_result.tool_calls;
    generate_step.tool_results = step_result.tool_results;
    generate_step.finish_reason = step_result.finish_reason;
    generate_step.usage = step_result.usage;
    final_result.steps.push_back(generate_step);

    if (initial_options.on_step_finish) {
      initial_options.on_step_finish.value()(generate_step);
    }

    // Roll up cumulative state on the final result.
    final_result.text += step_result.text;
    final_result.tool_calls.insert(final_result.tool_calls.end(),
                                   step_result.tool_calls.begin(),
                                   step_result.tool_calls.end());
    final_result.tool_results.insert(final_result.tool_results.end(),
                                     step_result.tool_results.begin(),
                                     step_result.tool_results.end());
    final_result.usage.prompt_tokens = step_result.usage.prompt_tokens;
    final_result.usage.completion_tokens += step_result.usage.completion_tokens;
    final_result.usage.total_tokens =
        final_result.usage.prompt_tokens + final_result.usage.completion_tokens;
    final_result.finish_reason = step_result.finish_reason;
    final_result.id = step_result.id;
    final_result.model = step_result.model;
    final_result.created = step_result.created;
    final_result.system_fingerprint = step_result.system_fingerprint;
    final_result.provider_metadata = step_result.provider_metadata;

    // Termination conditions: abort only on hard provider errors or content filters.
    if (step_result.finish_reason == kFinishReasonContentFilter ||
        step_result.finish_reason == kFinishReasonError) {
      pending_tool_results = false;
      break;
    }

    // Stuck-loop detection: three consecutive byte-identical steps (same
    // text, same tool calls, same tool results) mean the model is spinning.
    // Stop instead of burning tokens forever.
    const uint64_t step_fp = fingerprint_step(step_result);
    if (step_fp == prev_step_fp) {
      ++fp_streak;
    } else {
      prev_step_fp = step_fp;
      fp_streak = 1;
    }
    if (step_result.has_tool_calls() && fp_streak >= kMaxIdenticalSteps) {
      LOG_WARN(
          "MultiStepCoordinator: stuck loop detected - step {} repeated an "
          "identical request/result {} times; stopping",
          step + 1, fp_streak);
      final_result.finish_reason = kFinishReasonError;
      final_result.error =
          "Stuck loop detected: the same tool call and result repeated " +
          std::to_string(fp_streak) +
          " times. Refine the tool arguments or take a different approach.";
      pending_tool_results = false;
      break;
    }

    // Process tool calls if present, regardless of finish_reason ("tool_calls", "stop", etc.)
    if (step_result.has_tool_calls()) {
      const std::vector<ToolResult>& tool_results = step_result.tool_results;

      // Append assistant turn (text + tool calls) and tool result messages to
      // the running accumulator. Next iteration's input messages are rebuilt
      // from `initial_messages + response_messages` at the top of the loop.
      std::vector<ToolCallContentPart> tool_call_contents;
      tool_call_contents.reserve(step_result.tool_calls.size());
      for (const auto& tc : step_result.tool_calls) {
        tool_call_contents.emplace_back(tc.id, tc.tool_name, tc.arguments, tc.thought_signature);
      }
      response_messages.push_back(
          Message::assistant_with_tools(step_result.text, tool_call_contents));

      Messages tool_messages =
          tool_results_to_messages(step_result.tool_calls, tool_results);
      if (step_result.usage.prompt_tokens > 0) {
        live_context = static_cast<size_t>(step_result.usage.prompt_tokens) +
                       static_cast<size_t>(step_result.usage.completion_tokens) +
                       estimate_tokens(tool_messages);
      } else {
        live_context = estimate_tokens(step_messages) + estimate_tokens(tool_messages);
      }
      response_messages.insert(response_messages.end(), tool_messages.begin(),
                               tool_messages.end());
      pending_tool_results = true;
    } else {
      // No tool calls: text response completed.
      pending_tool_results = false;
      break;
    }
  }

  // Hit the step cap with tool results still pending: run one final
  // synthesis pass with tools disabled so the model converts those results
  // into a closing answer instead of being cut off mid-work.
  if (pending_tool_results && step_cap > 0 &&
      !(initial_options.abort_flag && initial_options.abort_flag->load())) {
    LOG_WARN("MultiStepCoordinator: reached step cap ({}) with pending tool "
             "results; running one synthesis step without tools",
             step_cap);
    GenerateOptions synth_options = step_options;
    synth_options.tools.clear();
    synth_options.active_tools.clear();
    synth_options.max_steps = 1;
    synth_options.prompt.clear();
    // Rebuild input = immutable prefix + full accumulator (including the
    // final tool results appended by the last iteration).
    Messages synth_messages(step_messages.begin(),
                            step_messages.begin() +
                                static_cast<std::ptrdiff_t>(initial_count));
    synth_messages.insert(synth_messages.end(), response_messages.begin(),
                          response_messages.end());
    synth_options.messages = std::move(synth_messages);

    GenerateResult synth = generate_func(synth_options);
    if (synth.is_success()) {
      GenerateStep synth_step;
      synth_step.text = synth.text;
      synth_step.finish_reason = synth.finish_reason;
      synth_step.usage = synth.usage;
      final_result.steps.push_back(synth_step);
      if (initial_options.on_step_finish) {
        initial_options.on_step_finish.value()(synth_step);
      }
      final_result.text += synth.text;
      final_result.usage.prompt_tokens = synth.usage.prompt_tokens;
      final_result.usage.completion_tokens += synth.usage.completion_tokens;
      final_result.usage.total_tokens = final_result.usage.prompt_tokens +
                                        final_result.usage.completion_tokens;
      final_result.finish_reason = synth.finish_reason;
      final_result.id = synth.id;
      final_result.model = synth.model;
      final_result.created = synth.created;
      final_result.system_fingerprint = synth.system_fingerprint;
      final_result.provider_metadata = synth.provider_metadata;
      response_messages.push_back(Message::assistant(synth.text));
    } else {
      LOG_WARN("MultiStepCoordinator: synthesis step failed: {}",
               synth.error_message());
    }
  }

  // Surface the accumulated assistant/tool messages on the result for callers
  // that want to continue the conversation.
  final_result.response_messages.insert(final_result.response_messages.end(),
                                        response_messages.begin(),
                                        response_messages.end());

  if (step_cap > 0 &&
      final_result.steps.size() ==
          static_cast<size_t>(step_cap) &&
      final_result.finish_reason != kFinishReasonStop) {
    LOG_DEBUG("Reached max steps limit ({}) without completion",
                          step_cap);
  }

  return final_result;
}

Messages MultiStepCoordinator::tool_results_to_messages(
    const std::vector<ToolCall>& tool_calls,
    const std::vector<ToolResult>& tool_results) {
  Messages messages;

  std::map<std::string, ToolResult> results_by_id;
  for (const auto& result : tool_results) {
    results_by_id[result.tool_call_id] = result;
  }

  std::vector<ToolResultContentPart> tool_result_contents;
  for (const auto& tool_call : tool_calls) {
    auto result_it = results_by_id.find(tool_call.id);
    if (result_it != results_by_id.end()) {
      const ToolResult& result = result_it->second;
      if (result.is_success()) {
        tool_result_contents.emplace_back(tool_call.id, result.result, false);
      } else {
        JsonValue error_json = {{"error", result.error_message()}};
        tool_result_contents.emplace_back(tool_call.id, error_json, true);
      }
    }
  }

  if (!tool_result_contents.empty()) {
    messages.push_back(Message::tool_results(tool_result_contents));
  }

  return messages;
}

}  // namespace qcode