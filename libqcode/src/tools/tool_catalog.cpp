#include <qcode/tools/tool_catalog.h>

#include <sstream>
#include <qcode/core/logger.h>
#include <nlohmann/json.hpp>

#include <iomanip>

#include <qcode/tools/bash_tool.h>
#include <qcode/tools/task_tool.h>
#include <qcode/tools/image_tool.h>

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
      "Delegate a self-contained job to a subagent and wait for its report. "
      "Several task calls in one message run in parallel.",
      R"(task = { prompt: string, description?: string,
         mode?: "explore"|"implement"|"verify",
         difficulty?: "easy"|"medium"|"hard", model?: "provider:model" })",
      true
    },
    {
      "rate_task",
      "Rate finished task reports 1-5 so future tasks go to the free models that do well.",
      R"(rate_task = { ratings: { task_id: string, score: 1..5, note?: string }[] })",
      true
    },
    {
      "image",
      "Load a local image (png, jpeg, webp, gif; max 3.75 MB) so you can see it.",
      R"(image = { path: string })",
      true
    },
  };
}

std::string ToolCatalog::build_tool_section(const ToolConfig& cfg) {
  LOG_DEBUG("Tools: build_tool_section bash={} task={}", cfg.enable_bash, cfg.enable_task);
  std::ostringstream ss;
  ss << "### Core Tool Contract\n\n";
  ss << "This base prompt is the only system-prompt location for tool-use policy.\n\n";
  if (cfg.enable_task && cfg.enable_image) {
    ss << "The main purpose of this section is to define the purpose of tool calls "
          "and how to use them. You have three sets of tools available:\n\n"
          "1. **Bash tool** - works as a Swiss Army knife; it can execute anything in the shell and read the output. Prefer relative paths under the session workspace (on Android: app sandbox `$HOME`; do not use `/tmp` or `/` — use `$HOME/tmp` / `$TMPDIR` for temporary files).\n"
          "2. **Task tools** - `task` delegates a job to a subagent; `rate_task` scores its report 1-5 so future jobs go to models that do well.\n"
          "3. **Image tool** - loads a local png/jpeg/webp/gif image so you can see it.\n\n";
  } else if (cfg.enable_task) {
    ss << "The main purpose of this section is to define the purpose of tool calls "
          "and how to use them. You have two sets of tools available:\n\n"
          "1. **Bash tool** - works as a Swiss Army knife; it can execute anything in the shell and read the output. Prefer relative paths under the session workspace (on Android: app sandbox `$HOME`; do not use `/tmp` or `/` — use `$HOME/tmp` / `$TMPDIR` for temporary files).\n"
          "2. **Task tools** - `task` delegates a job to a subagent; `rate_task` scores its report 1-5 so future jobs go to models that do well.\n\n";
  } else if (cfg.enable_image) {
    ss << "The main purpose of this section is to define the purpose of tool calls "
          "and how to use them. You have two sets of tools available:\n\n"
          "1. **Bash tool** - works as a Swiss Army knife; it can execute anything in the shell and read the output. Prefer relative paths under the session workspace (on Android: app sandbox `$HOME`; do not use `/tmp` or `/` — use `$HOME/tmp` / `$TMPDIR` for temporary files).\n"
          "2. **Image tool** - loads a local png/jpeg/webp/gif image so you can see it.\n\n";
  } else {
    ss << "The main purpose of this section is to define the purpose of tool calls "
          "and how to use them. The only exposed tool is **bash**.\n\n";
    ss << "**Bash** is the Swiss Army knife: run any shell command and read the "
          "output. Prefer relative paths under the session workspace (on Android: "
          "app sandbox `$HOME`; do not use `/tmp` or `/` — use `$HOME/tmp` / "
          "`$TMPDIR` for temporary files). Do not expect read/write/grep/ls/"
          "task tools; do those jobs with bash.\n\n";
  }
  ss << "Here is the schema:\n\n";
  ss << "```\n";

  for (const auto& d : descriptors()) {
    if ((d.name == "bash" && !cfg.enable_bash) ||
        ((d.name == "task" || d.name == "rate_task") && !cfg.enable_task) ||
        (d.name == "image" && !cfg.enable_image))
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
  LOG_DEBUG("Tools: build_definitions bash={} task={} image={}", cfg.enable_bash, cfg.enable_task, cfg.enable_image);
  qcode::ToolSet tools;
  if (cfg.enable_bash)
    tools["bash"] = qcode::BashTool::definition();
  if (cfg.enable_task) {
    tools["task"] = qcode::TaskTool::definition();
    tools["rate_task"] = qcode::TaskTool::rate_definition();
  }
  if (cfg.enable_image)
    tools["image"] = qcode::ImageTool::definition();
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
    } else if (tool_name == "image") {
      if (json.contains("path")) {
        formatted += "  Path: " + json["path"].get<std::string>() + "\n";
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

} // namespace qcode
