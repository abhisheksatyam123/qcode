#include <ai/tui/message_render.h>
#include <ai/tui/views.h>

namespace ai {
namespace tui {

using namespace ftxui;

Element render_message(const ai::Message& msg, const ChatState& state, const std::vector<ProviderInfo>& providers_list, int selected_provider, int selected_model, const std::string& theme) {
    Elements parts;
    
    // Role label
    std::string role_label = (msg.role == ai::kMessageRoleUser) ? "You" : "Assistant";
    Color role_color = (msg.role == ai::kMessageRoleUser) ? user_green() : accent2(theme);
    
    // Add header
    parts.push_back(hbox({
        text("  " + role_label) | bold | color(role_color),
        text(" · " + providers_list[selected_provider].models[selected_model].name) | dim | color(accent(theme))
    }));

    // Iterate parts
    for (const auto& part : msg.content) {
        if (const auto* text_part = std::get_if<ai::TextContentPart>(&part)) {
            parts.push_back(vbox(render_markdown(text_part->text)));
        } else if (const auto* tool_part = std::get_if<ai::ToolCallContentPart>(&part)) {
            parts.push_back(text("  🔧 Tool Call: " + tool_part->tool_name) | color(Color::RGB(180, 220, 120)));
        }
    }
    
    return hbox({
        separatorLight() | color(role_color),
        vbox(std::move(parts)) | flex
    });
}

} // namespace tui
} // namespace ai
