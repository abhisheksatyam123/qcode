#include <ai/tui/tools.h>

#include <sstream>

#include <ai/tools/bash_tool.h>
#include <ai/tools/task_tool.h>

namespace ai {
namespace tui {

std::vector<ToolDescriptor> Tools::descriptors() {
  return {
    {
      "bash",
      "Swiss-army-knife shell executor (run, background, list, status, kill, remove, cleanup).",
      R"(bash =
  | { mode?: "run", command: string, workdir?: string, timeout?: number,
      auto_background?: boolean, max_output_chars?: number,
      max_output_lines?: number, description?: string }
  | { mode: "background", command: string, workdir?: string, timeout?: number,
      auto_background?: boolean, description?: string })",
      true
    },
    {
      "task",
      "Delegate bounded work to subagents (spawn, result, kill, pause, resume, resurrect).",
      R"(task.spawn = { op?: "spawn", description: string, subagent_type: string,
    prompt: string, mode?: "explore"|"implement"|"verify",
    objective?: string, scope?: string[], out_of_scope?: string[],
    filesystem_policy?: "bash-only", output_format?: "structured-summary",
    can_edit?: boolean, allowed_paths?: string[], forbidden_paths?: string[],
    budget?: { max_files?: number, max_output_chars?: number, timeout_ms?: number },
    model?: string, models?: string[], task_id?: string,
    background?: boolean, run_in_background?: boolean }
task.result = { op: "result", background_task_id: string, timeout_ms?: number }
task.lifecycle/model = { op: "kill"|"pause"|"resume"|"resurrect"|"model",
    task_id?: string, pid?: string, reason?: string, model?: string })",
      true
    },
  };
}

std::string Tools::build_tool_section(const ToolConfig& cfg) {
  std::ostringstream ss;
  ss << "### Core Tool Contract\n\n";
  ss << "This base prompt is the only system-prompt location for tool-use policy.\n\n";
  ss << "The main purpose of this section is to define the purpose of tool calls "
        "and how to use them. You have two sets of tools available:\n\n";
  ss << "1. **Bash tool** - works as a Swiss Army knife; it can execute anything "
        "in the shell and read the output.\n";
  ss << "2. **Task tool** - used to delegate tasks to other agents.\n\n";
  ss << "Here is the schema of both:\n\n";
  ss << "```\n";

  for (const auto& d : descriptors()) {
    if ((d.name == "bash" && !cfg.enable_bash) ||
        (d.name == "task" && !cfg.enable_task))
      continue;
    ss << d.schema_text << "\n\n";
  }

  ss << "```\n\n";
  ss << "### How to Use Tools\n\n";
  ss << "1. For investigation or fix requests, gather high-signal context before "
        "editing or answering. Prefer one comprehensive, bounded, read-only context "
        "script in the first tool call over many small tool turns.\n";
  ss << "2. The exposed execution tool is `bash`; use it to run tools in this "
        "priority: `bun`/`bunx` TypeScript or JavaScript first, then `python`, "
        "then plain shell.\n";
  ss << "3. All secondary/local helper tools are stored in the notes vault tools "
        "directory. Prefer those reusable tools when they fit the task.\n";
  ss << "4. Keep context scripts bounded and readable: print section headers, "
        "summarize counts, use targeted searches/ranges/limits.\n";
  ss << "5. Keep mutating or stateful actions separate unless explicitly asked.\n";

  return ss.str();
}

ai::ToolSet Tools::build_definitions(const ToolConfig& cfg) {
  ai::ToolSet tools;
  if (cfg.enable_bash)
    tools["bash"] = ai::BashTool::definition();
  if (cfg.enable_task)
    tools["task"] = ai::TaskTool::definition();
  return tools;
}

std::string Tools::format_tool_call(const std::string& tool_name,
                                    const std::string& args) {
  return "\xF0\x9F\x94\xA7 " + tool_name + ": " + args;
}

std::string Tools::format_tool_result(const std::string& tool_name,
                                      bool success,
                                      const std::string& result_or_error,
                                      int truncate_at) {
  std::string s = result_or_error;
  if (static_cast<int>(s.length()) > truncate_at)
    s = s.substr(0, truncate_at) + "...";
  if (success)
    return "\xE2\x9C\x85 " + tool_name + ": " + s;
  else
    return "\xE2\x9D\x8C " + tool_name + ": " + s;
}

} // namespace tui
} // namespace ai
