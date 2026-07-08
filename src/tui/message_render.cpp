#include <sstream>
#include <ai/tui/tool_renderers.h>
#include <ai/tui/message_render.h>
#include <ai/tui/views.h>
#include <nlohmann/json.hpp>
#include <ai/logger.h>

namespace ai {
namespace tui {

using namespace ftxui;

// ── BlockTool: generic collapsible-style tool block ──
// Kept unchanged — still used by tool_renderers.cpp
Element BlockTool(const std::string& title, Element content,
                  bool is_running, const std::string& status,
                  Color border_color) {
  Elements header_parts;

  // Icon + title - only render if we have a title
  if (!title.empty()) {
    if (is_running) {
      header_parts.push_back(text(" ⠋ " + title) | dim);
    } else {
      header_parts.push_back(text(" " + title) | dim);
    }
  }

  // Status badge
  if (!status.empty()) {
    auto badge_color = border_color;
    header_parts.push_back(
        text(" " + status + " ") | color(badge_color) | bold);
  }

  return vbox({
      hbox(std::move(header_parts)),
      hbox({
          separatorLight() | color(border_color),
          vbox(std::move(content)) | flex,
      }),
  });
}

// ── Extract a concise summary string for a tool call ──
// For "bash" tools: show `$ <command>`
// For other tools: show the first string argument value (truncated)
static std::string extract_tool_summary(const ai::ToolCallContentPart& part) {
  const auto& args = part.arguments;
  if (!args.is_object()) return args.dump();

  // Bash / shell: show the command
  if (part.tool_name == "bash" || part.tool_name == "shell" ||
      part.tool_name == "run_command") {
    for (const auto& key : {"command", "cmd", "script"}) {
      if (args.contains(key) && args[key].is_string()) {
        std::string cmd = args[key].get<std::string>();
        if (cmd.size() > 120) cmd = cmd.substr(0, 117) + "...";
        return "$ " + cmd;
      }
    }
  }

  // Read/write file: show the path
  if (part.tool_name == "read_file" || part.tool_name == "write_file" ||
      part.tool_name == "view_file" || part.tool_name == "edit_file") {
    for (const auto& key : {"path", "file", "file_path", "filename"}) {
      if (args.contains(key) && args[key].is_string()) {
        return args[key].get<std::string>();
      }
    }
  }

  // Search / grep
  if (part.tool_name == "search" || part.tool_name == "grep" ||
      part.tool_name == "ripgrep") {
    if (args.contains("query") && args["query"].is_string()) {
      std::string q = args["query"].get<std::string>();
      if (q.size() > 80) q = q.substr(0, 77) + "...";
      return "\"" + q + "\"";
    }
  }

  // Task / subagent
  if (part.tool_name == "task" || part.tool_name == "dispatch_agent") {
    for (const auto& key : {"description", "prompt", "task"}) {
      if (args.contains(key) && args[key].is_string()) {
        std::string desc = args[key].get<std::string>();
        if (desc.size() > 100) desc = desc.substr(0, 97) + "...";
        return desc;
      }
    }
  }

  // Generic fallback: first string value
  for (auto it = args.begin(); it != args.end(); ++it) {
    if (it.value().is_string()) {
      std::string val = it.value().get<std::string>();
      if (val.size() > 100) val = val.substr(0, 97) + "...";
      return val;
    }
  }

  // Last resort: compact JSON
  auto s = args.dump();
  if (s.size() > 80) s = s.substr(0, 77) + "...";
  return s;
}

// ── Render a tool call as a collapsed one-liner ──
// Style: `▶ tool_name  summary_text`   ⠋ calling...
static Element render_tool_call(const ai::ToolCallContentPart& part,
                                const std::string& theme) {
  LOG_DEBUG("render_tool_call: tool_name={} id={}", part.tool_name, part.id);

  std::string summary = extract_tool_summary(part);

  return hbox({
      text("  ▶ ") | color(accent(theme)),
      text(part.tool_name) | bold | color(accent(theme)),
      text("  " + summary) | dim,
      filler(),
      text(" ⠋ calling... ") | color(Color::Yellow),
  });
}

// ── Render a tool result with OpenCode-style pretty view ──
// Shows title, command, output, exit code, and timing in a styled block
static Element render_tool_result(const ai::ToolResultContentPart& part,
                                  const std::string& theme) {
  LOG_DEBUG("render_tool_result: tool_call_id={} is_error={}", part.tool_call_id,
            part.is_error);

  Elements content;

  if (part.is_error) {
    content.push_back(hbox({
        text("    ✗ ") | color(Color::Red),
        text(" failed") | color(Color::Red) | bold,
    }));
    // Show error details if available
    std::string err_detail;
    if (part.result.is_string()) {
      err_detail = part.result.get<std::string>();
    } else if (part.result.is_object() && part.result.contains("error")) {
      err_detail = part.result["error"].get<std::string>();
    }
    if (!err_detail.empty()) {
      content.push_back(hbox({
          text("      ") | dim,
          text(err_detail) | dim,
      }));
    }
    return vbox(std::move(content));
  }

  // Try to extract structured fields from result JSON
  std::string title;
  std::string output;
  std::string command;
  std::string workdir;
  int exit_code = 0;
  bool has_exit_code = false;
  bool has_output = false;

  if (part.result.is_object()) {
    // Common tool result format: { title, output, metadata: { exit, ... } }
    if (part.result.contains("title") && part.result["title"].is_string()) {
      title = part.result["title"].get<std::string>();
    }
    if (part.result.contains("output") && part.result["output"].is_string()) {
      output = part.result["output"].get<std::string>();
      has_output = !output.empty();
    }
    // Check for command in metadata
    if (part.result.contains("metadata") && part.result["metadata"].is_object()) {
      auto& meta = part.result["metadata"];
      if (meta.contains("exit")) {
        exit_code = meta["exit"].get<int>();
        has_exit_code = true;
      }
      if (meta.contains("description")) {
        if (title.empty() && meta["description"].is_string()) {
          title = meta["description"].get<std::string>();
        }
      }
    }
    // Check for command at top level (some tools store command directly)
    if (part.result.contains("command") && part.result["command"].is_string()) {
      command = part.result["command"].get<std::string>();
    }
    // Check for workdir
    if (part.result.contains("workdir") && part.result["workdir"].is_string()) {
      workdir = part.result["workdir"].get<std::string>();
    }
  } else if (part.result.is_string()) {
    // Plain string result - use as output
    output = part.result.get<std::string>();
    has_output = !output.empty();
  }

  // ── Title/description line (like "# Verify the binary... · 66ms") ──
  Elements header_parts;
  if (!title.empty()) {
    header_parts.push_back(text("  # " + title) | dim | color(dim_gray()));
  } else {
    header_parts.push_back(text("  # task completed") | dim | color(dim_gray()));
  }
  // Show timing
  if (part.duration_ms > 0) {
    std::string timing;
    if (part.duration_ms < 1000) {
      timing = " · " + std::to_string((int)part.duration_ms) + "ms";
    } else {
      timing = " · " + std::to_string((int)(part.duration_ms / 100.0) / 10.0) + "s";
    }
    header_parts.push_back(text(timing) | dim | color(dim_gray()));
  }
  content.push_back(hbox(std::move(header_parts)));

  // Blank line
  content.push_back(text(""));

  // ── Command line (like "$ ls -la ...") ──
  if (!command.empty()) {
    content.push_back(hbox({
        text("  $ ") | color(Color::RGB(100, 100, 255)),
        text(command) | bold | color(Color::RGB(220, 220, 220)),
    }));
    // Blank line
    content.push_back(text(""));
  }

  // ── Exit code line (like "  ● exit 0") ──
  if (has_exit_code) {
    Color exit_color = (exit_code == 0) ? Color::Green : Color::Red;
    content.push_back(hbox({
        text("  ") | dim,
        text("●") | color(exit_color),
        text(" exit " + std::to_string(exit_code)) | color(exit_color),
        text("  ") | dim,
    }));
    // Blank line
    content.push_back(text(""));
  }

  // ── Output content ──
  if (has_output) {
    // Split output into lines and render each indented with line numbers
    Elements output_lines;
    std::istringstream stream(output);
    std::string line;
    int line_num = 1;
    while (std::getline(stream, line)) {
      std::string num_str = std::to_string(line_num);
      // Right-align line numbers (up to 4 digits)
      while (num_str.size() < 4) num_str = " " + num_str;
      output_lines.push_back(hbox({
          text("  " + num_str) | dim | color(dim_gray()),
          text(" " + line) | color(Color::RGB(200, 200, 200)),
      }));
      line_num++;
      if (line_num > 500) { // Safety limit
        output_lines.push_back(hbox({
            text("  ...") | dim | color(Color::Yellow),
            text(" (output truncated)") | dim,
        }));
        break;
      }
    }
    content.push_back(vbox(std::move(output_lines)));
  }

  return vbox(std::move(content));
}

// ── Render a full message ──
Element render_message(const ai::Message& msg, const ChatState& state,
                       const std::vector<ProviderInfo>& providers_list,
                       int selected_provider, int selected_model,
                       const std::string& theme) {
  LOG_DEBUG("render_message: role={} content_parts={}",
            msg.roleToString(), msg.content.size());

  Elements parts;

  // Classify content
  bool has_text = msg.has_text();
  bool has_tool_calls = msg.has_tool_calls();
  bool has_tool_results = msg.has_tool_results();

  LOG_DEBUG("render_message: has_text={} has_tool_calls={} has_tool_results={}",
            has_text, has_tool_calls, has_tool_results);

  // ── Role header ──
  if (msg.role == kMessageRoleAssistant && has_text) {
    // ❯ Assistant · model_name
    std::string model_label;
    if (selected_provider >= 0 &&
        selected_provider < static_cast<int>(providers_list.size()) &&
        selected_model >= 0 &&
        selected_model < static_cast<int>(
            providers_list[selected_provider].models.size())) {
      model_label =
          providers_list[selected_provider].models[selected_model].name;
    }

    Elements header;
    header.push_back(text("❯ ") | bold | color(accent(theme)));
    header.push_back(text("Assistant") | bold | color(accent(theme)));
    if (!model_label.empty()) {
      header.push_back(text(" · " + model_label) | dim | color(dim_gray()));
    }
    parts.push_back(hbox(std::move(header)));
  } else if (msg.role == kMessageRoleUser && !has_tool_results) {
    // ❯ You
    parts.push_back(hbox({
        text("❯ ") | bold | color(user_green()),
        text("You") | bold | color(user_green()),
    }));
  } else if (msg.role == kMessageRoleSystem) {
    // ℹ System
    parts.push_back(hbox({
        text("ℹ ") | dim | color(dim_gray()),
        text("System") | dim | color(dim_gray()),
    }));
  }

  // ── Content parts ──
  for (const auto& part : msg.content) {
    if (const auto* text_part = std::get_if<ai::TextContentPart>(&part)) {
      LOG_DEBUG("render_message: dispatching TextContentPart len={}",
                text_part->text.size());
      if (!text_part->text.empty()) {
        if (msg.role == kMessageRoleSystem) {
          // System messages: render dim
          parts.push_back(
              hbox({text("  "), text(text_part->text) | dim}));
        } else {
          // User / assistant: indented markdown
          Elements md = render_markdown(text_part->text);
          // Indent each line by 2 spaces
          Elements indented;
          for (auto& el : md) {
            indented.push_back(hbox({text("  "), std::move(el)}));
          }
          parts.push_back(vbox(std::move(indented)));
        }
      }
    } else if (const auto* tool_part =
                   std::get_if<ai::ToolCallContentPart>(&part)) {
      LOG_DEBUG("render_message: dispatching ToolCallContentPart name={}",
                tool_part->tool_name);
      parts.push_back(render_tool_call(*tool_part, theme));
    } else if (const auto* result_part =
                   std::get_if<ai::ToolResultContentPart>(&part)) {
      LOG_DEBUG("render_message: dispatching ToolResultContentPart id={}",
                result_part->tool_call_id);
      parts.push_back(render_tool_result(*result_part, theme));
    }
  }

  return vbox(std::move(parts));
}

}  // namespace tui
}  // namespace ai
