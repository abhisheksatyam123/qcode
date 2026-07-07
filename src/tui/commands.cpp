#include <ai/tui/commands.h>
#include <ai/tui/db.h>

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

    if (cmd == "theme" || cmd == "themes") {
        if (args.empty()) {
            std::string cur = state.theme ? *state.theme : "orange";
            state.chat_history->push_back({"System", "Available themes:\n  /theme orange (Classic)\n  /theme green (Forest Green)\n  /theme blue (Deep Blue)\n  /theme purple (Cyberpunk Purple)\n  /theme monochrome (Monochrome)\n\nCurrent theme: " + cur});
            return true;
        }
        std::string new_theme = args;
        std::transform(new_theme.begin(), new_theme.end(), new_theme.begin(), ::tolower);
        new_theme.erase(new_theme.begin(), std::find_if(new_theme.begin(), new_theme.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        new_theme.erase(std::find_if(new_theme.rbegin(), new_theme.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), new_theme.end());

        if (new_theme == "orange" || new_theme == "green" || new_theme == "blue" || new_theme == "purple" || new_theme == "monochrome") {
            if (state.theme) {
                *state.theme = new_theme;
            }
            state.chat_history->push_back({"System", "Theme changed to: " + new_theme});
        } else {
            state.chat_history->push_back({"System", "Unknown theme: '" + new_theme + "'. Supported: orange, green, blue, purple, monochrome"});
        }
        return true;
    }

    if (cmd == "tools") {
        if (args.empty()) {
            enable_tools = !enable_tools;
            std::string status = enable_tools ? "ENABLED (observability mode)" : "DISABLED (real-time streaming mode)";
            state.chat_history->push_back({"System", "Tools are now " + status});
            return true;
        }
        std::string val = args;
        std::transform(val.begin(), val.end(), val.begin(), ::tolower);
        // Trim
        val.erase(val.begin(), std::find_if(val.begin(), val.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        val.erase(std::find_if(val.rbegin(), val.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), val.end());

        if (val == "on" || val == "enable" || val == "true") {
            enable_tools = true;
            state.chat_history->push_back({"System", "Tools are now ENABLED (observability mode)"});
        } else if (val == "off" || val == "disable" || val == "false") {
            enable_tools = false;
            state.chat_history->push_back({"System", "Tools are now DISABLED (real-time streaming mode)"});
        } else {
            state.chat_history->push_back({"System", "Usage: /tools [on|off]"});
        }
        return true;
    }

    if (cmd == "new") {
        std::string prov = providers_list[selected_provider].name;
        std::string mod = providers_list[selected_provider].models[selected_model].name;
        std::string new_id = db::create_new_session(prov, mod);
        *state.session_id = new_id;
        state.chat_history->clear();
        state.messages_history->clear();
        state.chat_history->push_back({"System", "Started new persistent session: " + new_id});
        return true;
    }

    if (cmd == "sessions" || cmd == "list") {
        auto list = db::list_sessions();
        std::string out = "Saved Sessions:\n";
        for (auto& s : list) {
            out += "  " + s.first + "  (" + s.second + ")\n";
        }
        state.chat_history->push_back({"System", out});
        return true;
    }

    if (cmd == "session" || cmd == "load") {
        std::string target_id = args;
        target_id.erase(target_id.begin(), std::find_if(target_id.begin(), target_id.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        target_id.erase(std::find_if(target_id.rbegin(), target_id.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), target_id.end());

        if (target_id.empty()) {
            state.chat_history->push_back({"System", "Usage: /session <session_id>"});
            return true;
        }

        auto list = db::list_sessions();
        bool found = false;
        for (auto& s : list) {
            if (s.first == target_id) {
                found = true;
                break;
            }
        }

        if (found) {
            *state.session_id = target_id;
            db::reload_session_history(target_id, state);
            state.chat_history->push_back({"System", "Loaded persistent session: " + target_id});
        } else {
            state.chat_history->push_back({"System", "Session ID not found: " + target_id});
        }
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
