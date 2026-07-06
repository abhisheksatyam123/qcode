#include <ai/tui/commands.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

namespace ai {
namespace tui {

std::vector<ModelEntry> build_model_entries(
    const std::vector<ProviderInfo>& providers
) {
    std::vector<ModelEntry> entries;
    for (size_t pi = 0; pi < providers.size(); pi++) {
        for (size_t mi = 0; mi < providers[pi].models.size(); mi++) {
            entries.push_back({
                static_cast<int>(pi),
                static_cast<int>(mi),
                providers[pi].name,
                providers[pi].models[mi].name,
                providers[pi].models[mi].id,
                providers[pi].name,
            });
        }
    }
    return entries;
}

bool handle_slash_command(
    const std::string& raw_cmd,
    std::string& prompt_input,
    std::vector<ProviderInfo>& providers_list,
    int& selected_provider,
    int& selected_model,
    bool& enable_tools,
    std::string& system_prompt,
    ChatState& state
) {
    std::string cmd = raw_cmd.substr(1);
    std::string args;
    auto sp = cmd.find(" ");
    if (sp != std::string::npos) {
        args = cmd.substr(sp + 1);
        cmd = cmd.substr(0, sp);
    }
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    if (cmd == "model" || cmd == "models") {
        // /model typed directly — list models in chat (popup is the primary path)
        auto entries = build_model_entries(providers_list);
        std::ostringstream m;
        m << "Select a model:\n";
        std::string last_cat;
        for (auto& e : entries) {
            if (e.category != last_cat) {
                if (!last_cat.empty()) m << "\n";
                m << "  " << e.category << ":\n";
                last_cat = e.category;
            }
            bool active = (e.provider_idx == selected_provider &&
                           e.model_idx == selected_model);
            m << "  " << (active ? "▶ " : "  ") << e.model_name;
            if (e.model_id != e.model_name)
                m << "  " << e.model_id;
            m << "\n";
        }
        state.chat_history->push_back({"System", m.str()});
        return true;
    }

    if (cmd == "help" || cmd == "?") {
        auto cmds = builtin_slash_commands();
        std::ostringstream h;
        h << "Commands:\n";
        for (auto& c : cmds)
            h << "  /" << c.name << "  — " << c.description << "\n";
        state.chat_history->push_back({"System", h.str()});
        return true;
    }

    state.chat_history->push_back({"System", "Unknown: /" + cmd + ". Try /help"});
    return true;
}

} // namespace tui
} // namespace ai
