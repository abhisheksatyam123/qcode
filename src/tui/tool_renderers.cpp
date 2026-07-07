#include <ai/tui/message_render.h>
#include <ftxui/dom/elements.hpp>

namespace ai {
namespace tui {

using namespace ftxui;

Element BashToolRender(const std::string& command, const std::string& output, int exit_code, bool is_running, const std::string& workdir) {
    Elements content;
    
    // Command string
    content.push_back(hbox({
        text("$ ") | color(Color::RGB(100, 100, 255)),
        text(command) | bold
    }));

    // Output rendering
    if (!output.empty()) {
        content.push_back(text(output) | dim);
    }
    
    std::string status = is_running ? "running" : (exit_code == 0 ? "success" : "failed");
    Color status_color = is_running ? Color::Yellow : (exit_code == 0 ? Color::Green : Color::Red);

    return BlockTool("Bash", vbox(std::move(content)), is_running, status, status_color);
}

} // namespace tui
} // namespace ai
