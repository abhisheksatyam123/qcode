#pragma once

#include <algorithm>
#include <cctype>
#include <ranges>
#include <string>
#include <string_view>

namespace qcode {

// Muse Spark (and similar chatty models) often end a turn with "I need X"
// / "one more round" and no tool calls. The tool loop would treat that as
// finished and leave the user's task half-done. These helpers decide when
// build mode should nudge the model to keep going.

inline std::string ascii_lower(std::string_view text) {
  std::string out(text.size(), '\0');
  std::ranges::transform(text, out.begin(), [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  });
  return out;
}

inline bool looks_like_task_completion(std::string_view text) {
  const auto lower = ascii_lower(text);
  static constexpr std::string_view kDone[] = {
      "done.",
      "done!",
      "done:",
      "done —",
      "done -",
      "\ndone",
      "**done",
      "## done",
      "done this run",
      "all set",
      "task is complete",
      "task complete",
      "task completed",
      "i've updated",
      "i have updated",
      "i've implemented",
      "i have implemented",
      "i've finished",
      "i have finished",
      "changes made",
      "finished.",
      "finished!",
      "finished:",
      "all tests green",
      "tests green",
      "tests pass",
      "tests passed",
      "summary:",
      "summary of changes",
      "here is a summary",
      "here's a summary",
  };
  return std::ranges::any_of(kDone, [&](std::string_view phrase) {
    return lower.find(phrase) != std::string::npos;
  });
}

inline bool looks_like_task_stall(std::string_view text) {
  const auto lower = ascii_lower(text);
  static constexpr std::string_view kStall[] = {
      "need your call",
      "need your confirmation",
      "need your input",
      "need your decision",
      "need your",
      "your call",
      "before i change",
      "before i proceed",
      "before i make",
      "now i need",
      "one more round",
      "one more step",
      "one more turn",
      "please confirm",
      "confirm whether",
      "confirm if",
      "to confirm, should i",
      "which option",
      "should i ",
      "should we ",
      "need the exact",
      "need the css",
      "need more information",
      "need more details",
      "need more context",
      "i need the",
      "wait —",
      "wait -",
      "next, i will",
      "next i will",
      "i will now",
      "i'll now",
      "let me check",
      "let me inspect",
      "let me verify",
      "let me test",
      "let me run",
      "let me look",
      "let me find",
      "let me investigate",
      "let me update",
      "let's check",
      "let's inspect",
      "let's verify",
      "let's test",
      "let's run",
      "proceeding to",
      "continuing to",
      "moving on to",
      "now checking",
      "now verifying",
      "now testing",
      "now running",
      "now inspecting",
      "now updating",
      "i will proceed",
      "i'll proceed",
      "i will start",
      "i'll start",
      "i will first",
      "i'll first",
      "i will continue",
      "i'll continue",
      "i will execute",
      "i'll execute",
      "i will investigate",
      "i'll investigate",
      "i will examine",
      "i'll examine",
      "i will edit",
      "i'll edit",
      "i will modify",
      "i'll modify",
      "i will create",
      "i'll create",
      "i will add",
      "i'll add",
      "i will implement",
      "i'll implement",
      "i will fix",
      "i'll fix",
      "i will check",
      "i'll check",
      "i will verify",
      "i'll verify",
      "i will test",
      "i'll test",
      "i will run",
      "i'll run",
      "i will look",
      "i'll look",
      "i will search",
      "i'll search",
      "i will read",
      "i'll read",
      "i will inspect",
      "i'll inspect",
      "i am going to",
      "i'm going to",
      "we will now",
      "we'll now",
      "we can now",
      "we should now",
      "we need to",
      "i need to",
      "next step",
      "next up",
      "next,",
      "moving to",
      "moving forward",
      "proceeding with",
      "continuing with",
      "starting with",
      "starting by",
      "first step",
      "first, i will",
      "first i will",
      "to fix this",
      "to resolve this",
      "to implement this",
      "to address this",
      "let's first",
      "let me first",
      "let's continue",
      "let's proceed",
      "let's move on",
      "let's start",
      "let me start",
      "let me read",
      "let me examine",
      "let me search",
      "let me edit",
      "let me modify",
      "let me write",
      "let me create",
      "let me fix",
      "shall i ",
      "should i proceed",
      "would you like me to",
      "do you want me to",
      "waiting for your",
      "pending your",
      "can proceed",
  };
  return std::ranges::any_of(kStall, [&](std::string_view phrase) {
    return lower.find(phrase) != std::string::npos;
  });
}

// Uncapped: build mode keeps nudging until task completion is detected.
// There is intentionally no numeric cap — continue_count is accepted for
// logging/telemetry only and never stops the turn. Empty text-only stops
// always continue. Non-empty text stops only on explicit completion signals;
// stall phrases are the primary signal, but any other non-completion text
// also continues so novel phrasing never halts mid-task. The user abort,
// queued-prompt yield, provider errors, and plan_mode remain the only stops.
inline bool should_auto_continue_build(bool plan_mode,
                                       int continue_count,
                                       std::string_view assistant_text) {
  (void)continue_count;
  if (plan_mode) return false;
  if (assistant_text.empty()) return true;
  if (looks_like_task_completion(assistant_text)) return false;
  if (looks_like_task_stall(assistant_text)) return true;
  return true;
}

inline constexpr std::string_view kBuildContinueNudge =
    "<system-reminder>\n"
    "Continue the user's task. Do not stop to ask for confirmation, scope "
    "picks, or more research unless you are blocked by a missing secret or "
    "a destructive action. Use tools to finish the remaining work. If the "
    "task is fully done, reply with a brief summary and no further tool "
    "calls.\n"
    "</system-reminder>";

inline constexpr std::string_view kOrchestratorReminder =
    "\n\n<system-reminder>\n"
    "# Orchestrator Mode - System Reminder\n\n"
    "You are the Lead Orchestrator agent. You coordinate and execute complex engineering tasks. "
    "You have full authority to plan, inspect, build, and verify. "
    "You can execute tasks directly using bash, or invoke subagents via the task tool to work on tasks in parallel. "
    "When facing broad investigations, complex refactors, multi-file searches, or parallel verification steps, "
    "spawn subagents (mode: explore | implement | verify) with `model: \"provider:model_id\"` "
    "so any lead can delegate to any catalog model. "
    "Monitor their status, collect their results, and synthesize their outputs to complete the user's objective.\n"
    "</system-reminder>";

inline constexpr std::string_view kBuildModeReminder =
    "\n\n<system-reminder>\n"
    "# Build Mode - System Reminder\n\n"
    "You are in BUILD mode. Keep using tools until the user's request is "
    "fully implemented. Do not stop mid-task to ask which option to pick "
    "or to announce the next research step — pick a reasonable default and "
    "continue. Ask a clarifying question only when you are blocked.\n"
    "</system-reminder>";

}  // namespace qcode
