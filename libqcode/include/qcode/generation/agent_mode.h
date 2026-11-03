#pragma once

#include <string>
#include <string_view>

namespace qcode {

// The two user-facing agent modes, plus the internal role of a delegated
// worker session.
//
//   normal        One agent does the work itself with bash (and image).
//                 No task tool, no model catalog in the prompt: the
//                 cheapest request shape.
//   orchestrator  The lead: plans, delegates self-contained jobs to
//                 subagent sessions with the task tool, monitors their
//                 background reports and synthesizes the result.
//   subagent      Not a mode the user picks. A child session started by
//                 the orchestrator runs as a focused worker.
inline constexpr std::string_view kAgentModeNormal = "normal";
inline constexpr std::string_view kAgentModeOrchestrator = "orchestrator";
inline constexpr std::string_view kAgentRoleSubagent = "subagent";

// New sessions start in this mode.
inline constexpr std::string_view kDefaultAgentMode = kAgentModeNormal;

// Maps any stored or requested value onto normal / orchestrator / subagent.
// Retired modes keep their closest behaviour: "autonomous" and "monitor"
// delegated, so they become orchestrator; "plan", "build" and unknown or
// empty values become normal.
inline std::string normalize_agent_mode(std::string_view mode) {
  if (mode == kAgentRoleSubagent) return std::string(kAgentRoleSubagent);
  if (mode == kAgentModeOrchestrator || mode == "autonomous" || mode == "monitor") {
    return std::string(kAgentModeOrchestrator);
  }
  return std::string(kAgentModeNormal);
}

inline bool is_orchestrator_mode(std::string_view mode) {
  return normalize_agent_mode(mode) == kAgentModeOrchestrator;
}

// normal <-> orchestrator. A subagent session keeps its role.
inline std::string next_agent_mode(std::string_view mode) {
  const std::string current = normalize_agent_mode(mode);
  if (current == kAgentRoleSubagent) return current;
  return std::string(current == kAgentModeOrchestrator ? kAgentModeNormal
                                                       : kAgentModeOrchestrator);
}

// Footer / picker label.
inline std::string agent_mode_label(std::string_view mode) {
  const std::string current = normalize_agent_mode(mode);
  if (current == kAgentRoleSubagent) return "Subagent";
  return current == kAgentModeOrchestrator ? "Orchestrator" : "Normal";
}

}  // namespace qcode
