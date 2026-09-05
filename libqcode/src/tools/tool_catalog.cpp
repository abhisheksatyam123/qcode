#include <qcode/tools/tool_catalog.h>

#include <sstream>
#include <qcode/core/logger.h>
#include <nlohmann/json.hpp>

#include <iomanip>

#include <qcode/tools/bash_tool.h>
#include <qcode/tools/task_tool.h>

namespace qcode {

std::vector<ToolDescriptor> ToolCatalog::descriptors() {
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

std::string ToolCatalog::build_tool_section(const ToolConfig& cfg) {
  LOG_DEBUG("Tools: build_tool_section bash={} task={}", cfg.enable_bash, cfg.enable_task);
  std::ostringstream ss;
  ss << "### Core Tool Contract\n\n";
  ss << "This base prompt is the only system-prompt location for tool-use policy.\n\n";
  ss << "The main purpose of this section is to define the purpose of tool calls "
        "and how to use them. The only exposed tool is **bash**.\n\n";
  ss << "**Bash** is the Swiss Army knife: run any shell command and read the "
        "output. Prefer relative paths under the session workspace (on Android: "
        "app sandbox `$HOME`; do not use `/tmp` or `/` — use `$HOME/tmp` / "
        "`$TMPDIR` for temporary files). Do not expect read/write/grep/ls/"
        "task tools; do those jobs with bash.\n\n";
  ss << "Here is the schema:\n\n";
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
#ifdef __ANDROID__
        "priority: `python3`/`python` first, then plain shell "
        "(TypeScript runtimes like bun are usually unavailable on Android).\n";
#else
        "priority: `bun`/`bunx` TypeScript or JavaScript first, then `python`, "
        "then plain shell.\n";
#endif
  ss << "3. All secondary/local helper tools are stored in the notes vault "
        "`tools/` directory (workspace-relative). Prefer those reusable tools "
        "when they fit the task. Common helpers:\n"
        "   - `python3 tools/curriculum_status.py .`  (class/subject/chapter gaps)\n"
        "   - `python3 tools/scaffold_chapter.py --class 9 --subject mathematics "
        "--slug ch02-... --title \"...\"`\n"
        "   - `python3 tools/record_attempt.py --path classes/.../ch01 "
        "--question-id q1 --correct 1 --topic ... [--refresh]`\n"
        "   - `python3 tools/websearch.py \"query\" [--num N] [--json]`\n"
        "   - `python3 tools/webfetch.py <url> [--max-chars N] [--json]`\n"
        "   - `python3 tools/list_notes.py <vault_root>`\n"
        "   - `python3 tools/extract_pdf.py <file.pdf> [out.md]`\n"
        "Curriculum content lives under `classes/<class>/<subject>/<chapter>/` "
        "with chapter quizzes and optional `quizzes/` for overall subject tests.\n";
  ss << "4. Keep context scripts bounded and readable: print section headers, "
        "summarize counts, use targeted searches/ranges/limits.\n";
  ss << "5. Keep mutating or stateful actions separate unless explicitly asked.\n";

  return ss.str();
}

qcode::ToolSet ToolCatalog::build_definitions(const ToolConfig& cfg) {
  LOG_DEBUG("Tools: build_definitions bash={} task={}", cfg.enable_bash, cfg.enable_task);
  qcode::ToolSet tools;
  if (cfg.enable_bash)
    tools["bash"] = qcode::BashTool::definition();
  if (cfg.enable_task)
    tools["task"] = qcode::TaskTool::definition();
  return tools;
}

// ── Pretty-print a JSON value with indentation ──
static std::string pretty_json(const nlohmann::json& j) {
  if (j.is_string()) return j.get<std::string>();
  return j.dump(2);
}

std::string ToolCatalog::format_tool_call(const std::string& tool_name,
                                    const std::string& args,
                                    int step_number,
                                    int max_steps) {
  std::string formatted;
  
  // Header
  formatted = "Tool Call · " + tool_name;
  if (max_steps > 0) {
    formatted += " (step " + std::to_string(step_number) + "/" + std::to_string(max_steps) + ")";
  }
  formatted += "\n";

  try {
    auto json = nlohmann::json::parse(args);
    
    if (tool_name == "bash") {
      // Show command prominently
      if (json.contains("command")) {
        formatted += "$\n";
        formatted += "  " + json["command"].get<std::string>() + "\n";
        formatted += "\n";
      }
      // Show all other fields as structured input
      formatted += "Input:\n";
      for (auto it = json.begin(); it != json.end(); ++it) {
        if (it.key() == "command") continue; // already shown
        std::string val;
        if (it.value().is_string()) val = it.value().get<std::string>();
        else val = it.value().dump();
        formatted += "  " + it.key() + ": " + val + "\n";
      }
      // If only command was present and nothing else, we still have Input
      if (json.size() <= 1) {
        formatted += "  (no additional parameters)\n";
      }
    } else if (tool_name == "task") {
      // Show description or prompt prominently
      if (json.contains("description")) {
        formatted += "Task: " + json["description"].get<std::string>() + "\n";
      }
      if (json.contains("prompt")) {
        formatted += "Prompt: " + json["prompt"].get<std::string>() + "\n";
      }
      if (json.contains("task")) {
        formatted += "Task: " + json["task"].get<std::string>() + "\n";
      }
      // Show all other fields
      formatted += "Input:\n";
      for (auto it = json.begin(); it != json.end(); ++it) {
        if (it.key() == "description" || it.key() == "prompt" || it.key() == "task") continue;
        std::string val;
        if (it.value().is_string()) val = it.value().get<std::string>();
        else val = it.value().dump();
        formatted += "  " + it.key() + ": " + val + "\n";
      }
      if (json.size() <= 1) {
        formatted += "  (no additional parameters)\n";
      }
    } else {
      // Generic tool: show all args
      formatted += "Input: " + pretty_json(json) + "\n";
    }
  } catch (...) {
    // If JSON parsing fails, show raw string
    formatted += "Input (raw): " + args + "\n";
  }
  
  // Trim trailing newline
  while (!formatted.empty() && formatted.back() == '\n')
    formatted.pop_back();
  
  return formatted;
}

std::string ToolCatalog::format_tool_result(const std::string& tool_name,
                                      bool success,
                                      const std::string& result_or_error,
                                      int truncate_at,
                                      double duration_seconds) {
  // Parse JSON FIRST, before any truncation
  nlohmann::json parsed;
  bool parsed_ok = false;
  try {
    parsed = nlohmann::json::parse(result_or_error);
    parsed_ok = true;
  } catch (...) {
    parsed_ok = false;
  }

  std::string formatted;
  
  // Status header
  if (success) {
    formatted = "Completed successfully";
    if (duration_seconds >= 0.0) {
      char buf[32];
      if (duration_seconds < 1.0) {
        std::snprintf(buf, sizeof(buf), " (%.0fms)", duration_seconds * 1000.0);
      } else if (duration_seconds < 60.0) {
        std::snprintf(buf, sizeof(buf), " (%.1fs)", duration_seconds);
      } else {
        std::snprintf(buf, sizeof(buf), " (%.1fm)", duration_seconds / 60.0);
      }
      formatted += buf;
    }
  } else {
    formatted = "Failed";
    if (duration_seconds >= 0.0) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), " (%.1fs)", duration_seconds);
      formatted += buf;
    }
  }
  formatted += "\n";

  // Output content
  if (tool_name == "bash" && success && parsed_ok) {
    // Bash success result: {title, output, metadata: {exit, description, backgrounded}}
    if (parsed.contains("output")) {
      std::string output = parsed["output"].get<std::string>();
      // Apply truncation AFTER parse, on the output field
      if (truncate_at > 0 && static_cast<int>(output.length()) > truncate_at)
        output = output.substr(0, truncate_at) + "...";
      
      if (!output.empty()) {
        // Indent output lines
        std::istringstream ss(output);
        std::string line;
        while (std::getline(ss, line)) {
          formatted += line + "\n";
        }
      }
    }
    // Show metadata (exit code etc)
    if (parsed.contains("metadata") && parsed["metadata"].is_object()) {
      auto& meta = parsed["metadata"];
      if (meta.contains("exit")) {
        formatted += "(exit: " + std::to_string(meta["exit"].get<int>()) + ")";
        if (meta.contains("backgrounded") && meta["backgrounded"].get<bool>()) {
          formatted += " [background]";
        }
        formatted += "\n";
      }
    }
    // Show title if present and output was empty
    if (parsed.contains("title") && (!parsed.contains("output") || parsed["output"].get<std::string>().empty())) {
      formatted += parsed["title"].get<std::string>() + "\n";
    }
  } else if (tool_name == "bash" && !success && parsed_ok) {
    // Bash failure result: {error: "..."} or has error field
    // Check if parsed has "error" key (bash returns {error: "..."} for validation errors)
    if (parsed.contains("error")) {
      std::string err = parsed["error"].get<std::string>();
      if (truncate_at > 0 && static_cast<int>(err.length()) > truncate_at)
        err = err.substr(0, truncate_at) + "...";
      formatted += err + "\n";
    } else {
      // Fallback
      std::string s = result_or_error;
      if (truncate_at > 0 && static_cast<int>(s.length()) > truncate_at)
        s = s.substr(0, truncate_at) + "...";
      formatted += s + "\n";
    }
  } else if (tool_name == "task" && parsed_ok) {
    // Task result
    if (success) {
      if (parsed.contains("result")) {
        std::string r = parsed["result"].dump(2);
        if (truncate_at > 0 && static_cast<int>(r.length()) > truncate_at)
          r = r.substr(0, truncate_at) + "...";
        formatted += r + "\n";
      } else if (parsed.contains("output")) {
        std::string output = parsed["output"].get<std::string>();
        if (truncate_at > 0 && static_cast<int>(output.length()) > truncate_at)
          output = output.substr(0, truncate_at) + "...";
        formatted += output + "\n";
      } else if (parsed.contains("summary")) {
        formatted += parsed["summary"].get<std::string>() + "\n";
      } else {
        std::string r = parsed.dump(2);
        if (truncate_at > 0 && static_cast<int>(r.length()) > truncate_at)
          r = r.substr(0, truncate_at) + "...";
        formatted += r + "\n";
      }
    } else {
      if (parsed.contains("error")) {
        formatted += parsed["error"].get<std::string>() + "\n";
      } else {
        std::string s = result_or_error;
        if (truncate_at > 0 && static_cast<int>(s.length()) > truncate_at)
          s = s.substr(0, truncate_at) + "...";
        formatted += s + "\n";
      }
    }
  } else {
    // Generic / unknown tool or unparseable JSON
    std::string s = result_or_error;
    if (parsed_ok) {
      s = parsed.dump(2);
    }
    if (truncate_at > 0 && static_cast<int>(s.length()) > truncate_at)
      s = s.substr(0, truncate_at) + "...";
    formatted += s + "\n";
  }
  
  // Trim trailing newline
  while (!formatted.empty() && formatted.back() == '\n')
    formatted.pop_back();
  
  return formatted;
}

} // namespace qcode
