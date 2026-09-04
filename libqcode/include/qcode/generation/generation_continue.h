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
      "all set",
      "task is complete",
      "i've updated",
      "i have updated",
      "i've implemented",
      "i have implemented",
      "changes made",
      "finished.",
  };
  return std::ranges::any_of(kDone, [&](std::string_view phrase) {
    return lower.find(phrase) != std::string::npos;
  });
}

inline bool looks_like_task_stall(std::string_view text) {
  const auto lower = ascii_lower(text);
  static constexpr std::string_view kStall[] = {
      "need your",
      "your call",
      "before i change",
      "before i ",
      "now i need",
      "one more",
      "confirm",
      "which option",
      "should i",
      "need the exact",
      "need the css",
      "need more",
      "i need the",
      "wait —",
      "wait -",
      "need your call",
  };
  return std::ranges::any_of(kStall, [&](std::string_view phrase) {
    return lower.find(phrase) != std::string::npos;
  });
}

// Returns true when the tool loop should inject a continue nudge instead of
// marking the turn finished.
inline bool should_auto_continue_build(bool plan_mode,
                                       int continue_count,
                                       std::string_view assistant_text) {
  constexpr int kMaxContinues = 6;
  constexpr int kMaxEmptyContinues = 3;
  if (plan_mode) return false;
  if (continue_count >= kMaxContinues) return false;
  if (assistant_text.empty()) return continue_count < kMaxEmptyContinues;
  if (looks_like_task_completion(assistant_text)) return false;
  return looks_like_task_stall(assistant_text);
}

inline constexpr std::string_view kBuildContinueNudge =
    "<system-reminder>\n"
    "Continue the user's task. Do not stop to ask for confirmation, scope "
    "picks, or more research unless you are blocked by a missing secret or "
    "a destructive action. Use tools to finish the remaining work. If the "
    "task is fully done, reply with a brief summary and no further tool "
    "calls.\n"
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
