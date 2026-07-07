#include <ai/tui/message_render.h>
#include <ai/tui/views.h>

namespace ai {
namespace tui {

using namespace ftxui;

Element BlockTool(const std::string& title, Element content, bool is_running, const std::string& status, Color border_color) {
    Elements header;
    if (is_running) {
        header.push_back(text(" ⠋ " + title) | dim);
    } else {
        header.push_back(text(" " + title) | dim);
        if (!status.empty()) header.push_back(text(" · " + status) | color(border_color));
    }
    
    return vbox({
        hbox(std::move(header)),
        hbox({
            separatorLight() | color(border_color),
            vbox(std::move(content)) | flex
        })
    });
}

Element render_message(const ai::Message& msg, const ChatState& state, const std::vector<ProviderInfo>& providers_list, int selected_provider, int selected_model, const std::string& theme) {
    Elements parts;
    
    // Header for Assistant
    if (msg.role == kMessageRoleAssistant) {
        parts.push_back(hbox({
            text("  Assistant") | bold | color(accent2(theme)),
            text(" · " + providers_list[selected_provider].models[selected_model].name) | dim | color(accent(theme))
        }));
    } else if (msg.role == kMessageRoleUser) {
        parts.push_back(text("  You") | bold | color(user_green()));
    }

    // Iterate parts
    for (const auto& part : msg.content) {
        if (const auto* text_part = std::get_if<ai::TextContentPart>(&part)) {
            parts.push_back(vbox(render_markdown(text_part->text)));
        } else if (const auto* tool_part = std::get_if<ai::ToolCallContentPart>(&part)) {
            parts.push_back(BlockTool("Tool Call: " + tool_part->tool_name, text(tool_part->arguments.dump()) | dim, false, "pending", Color::Yellow));
        } else if (const auto* result_part = std::get_if<ai::ToolResultContentPart>(&part)) {
            parts.push_back(BlockTool("Tool Result: " + result_part->tool_call_id, text(result_part->result.dump()) | dim, false, result_part->is_error ? "failed" : "success", result_part->is_error ? Color::Red : Color::Green));
        }
    }
    
    return vbox(std::move(parts));
}

} // namespace tui
} // namespace ai
