#include <ai/tui/commands.h>
#include <ai/tui/db.h>
#include <ai/logger.h>
#include <ai/types/client.h>
#include "ai/registry.h"
#include "ai/tui/provider_registry_init.h"
#include <memory>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <ctime>

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
    LOG_DEBUG("Commands: handle_slash_command cmd={}", cmd);
    std::string args;
    auto sp = cmd.find(" ");
    if (sp != std::string::npos) {
        args = cmd.substr(sp + 1);
        cmd = cmd.substr(0, sp);
    }
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    LOG_DEBUG("Commands: /model list requested");
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
        LOG_DEBUG("Commands: /theme args='{}'", args);
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
        LOG_DEBUG("Commands: /tools args='{}'", args);
        if (args.empty()) {
            enable_tools = !enable_tools;
            std::string status = enable_tools ? "ENABLED (observability mode)" : "DISABLED (real-time streaming mode)";
            state.chat_history->push_back({"System", "Tools are now " + status});
            return true;
        }
        std::string val = args;
        std::transform(val.begin(), val.end(), val.begin(), ::tolower);
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

    if (cmd == "compact") {
        LOG_DEBUG("Commands: /compact args='{}'", args);
        int keep = 4;
        if (!args.empty()) {
            try { keep = std::stoi(args); } catch (...) { keep = 4; }
        }
        if (keep < 0) keep = 0;

        const auto& hist = *state.messages_history;
        if (hist.size() <= 2) {
            state.chat_history->push_back({"System", "Nothing to compact: conversation is too short."});
            return true;
        }

        // Build a transcript of the conversation so far.
        std::ostringstream transcript;
        for (const auto& m : hist) {
            std::string role_str;
            if (m.role == ai::kMessageRoleUser) role_str = "User";
            else if (m.role == ai::kMessageRoleAssistant) role_str = "Assistant";
            else if (m.role == ai::kMessageRoleSystem) role_str = "System";
            else role_str = "Message";
            std::string text;
            for (const auto& part : m.content) {
                if (const auto* tp = std::get_if<ai::TextContentPart>(&part)) {
                    text += tp->text + "\n";
                } else if (const auto* tcp = std::get_if<ai::ToolCallContentPart>(&part)) {
                    text += "[Tool call: " + tcp->tool_name + "]\n";
                } else if (const auto* trp = std::get_if<ai::ToolResultContentPart>(&part)) {
                    text += "[Tool result]\n";
                }
            }
            transcript << role_str << ": " << text << "\n";
        }

        // opencode-style compaction: produce a ## Tasks / ## Systems handoff packet.
        const std::string compaction_instruction =
            "Tools are available when needed, including bash for bounded read-only "
            "inspection. Do not modify files or create notes while generating the "
            "handoff summary.\n\n"
            "Summarize the following conversation into a concise handoff packet so a "
            "fresh session can take over. Preserve the next actionable task, verified "
            "evidence, blockers, and concise facts about the code, APIs, data "
            "structures, files, and user preferences that matter for the request.\n\n"
            "Keep only task state and concise facts.\n\n"
            "Output only:\n"
            "## Tasks\n"
            "## Systems\n\n"
            "Use tools only when they improve summary accuracy; otherwise answer directly.";

        // Resolve the active provider client (mirrors chat_bus generation setup).
        const auto& sel = providers_list[selected_provider];
        ai::providers::register_tui_providers();
        auto resolution = ai::providers::ProviderRegistry::instance().resolve(sel.id, sel.api_url);
        if (!resolution.ok()) {
            state.chat_history->push_back({"System", "Compaction failed: " + resolution.error});
            return true;
        }
        ai::Client client = std::move(resolution.client);

        ai::GenerateOptions opts;
        opts.model = providers_list[selected_provider].models[selected_model].id;
        opts.system = compaction_instruction;
        opts.messages = {ai::Message::user(transcript.str())};

        ai::GenerateResult res = client.generate_text(opts);
        if (res.error && !res.error->empty()) {
            state.chat_history->push_back({"System", "Compaction failed: " + *res.error});
            return true;
        }
        std::string summary = res.text;
        if (summary.empty()) {
            state.chat_history->push_back({"System", "Compaction failed: empty summary from model."});
            return true;
        }

        // Resolve the notes vault and write the handoff to a todo file.
        std::string notes_root = "/local/mnt/workspace/notes";
        if (const char* e = std::getenv("OPENCODE_NOTES_ROOT")) notes_root = e;
        else if (!std::filesystem::exists("/local/mnt/workspace/notes")) notes_root = "/home/abhi/notes";
        std::string sid = (state.session_id && !state.session_id->empty())
                              ? *state.session_id
                              : std::to_string(std::time(nullptr));
        std::string todo_dir = notes_root + "/scratchpad/task/qcode-tui/active/todo-" + sid;
        std::error_code ec;
        std::filesystem::create_directories(todo_dir, ec);
        std::string todo_path = todo_dir + "/todo.md";
        bool wrote = false;
        if (!ec) {
            std::ofstream out(todo_path);
            if (out) {
                out << "# qcode compacted handoff\n\n" << summary << "\n";
                wrote = true;
            }
        }

        // Compacted in-memory history: the handoff packet + the most recent turns.
        ai::Messages new_messages;
        std::string summary_body =
            "This conversation was compacted into a handoff packet" +
            (wrote ? (" written to: " + todo_path) : "") +
            ".\n\n" + summary;
        new_messages.push_back(ai::Message::user(summary_body));

        size_t start = (hist.size() > static_cast<size_t>(keep))
                           ? hist.size() - static_cast<size_t>(keep)
                           : 0;
        for (size_t i = start; i < hist.size(); ++i) {
            new_messages.push_back(hist[i]);
        }

        auto new_chat = std::make_shared<std::vector<std::pair<std::string, std::string>>>();
        std::string note = "Conversation compacted: " + std::to_string(hist.size()) +
                           " messages -> handoff packet";
        if (wrote) note += "\nTodo file: " + todo_path;
        else note += " (todo file write failed)";
        new_chat->push_back({"System", note});
        for (const auto& m : new_messages) {
            std::string role_str;
            if (m.role == ai::kMessageRoleUser) role_str = "User";
            else if (m.role == ai::kMessageRoleAssistant) role_str = "Assistant";
            else if (m.role == ai::kMessageRoleSystem) role_str = "System";
            else role_str = "Message";
            std::string text;
            for (const auto& part : m.content) {
                if (const auto* tp = std::get_if<ai::TextContentPart>(&part)) text += tp->text;
                else if (const auto* tcp = std::get_if<ai::ToolCallContentPart>(&part)) text += "[Tool call: " + tcp->tool_name + "]";
                else if (const auto* trp = std::get_if<ai::ToolResultContentPart>(&part)) text += "[Tool result]";
            }
            new_chat->push_back({role_str, text});
        }

        state.messages_history = std::make_shared<ai::Messages>(new_messages);
        state.chat_history = new_chat;
        return true;
    }


    state.chat_history->push_back({"System", "Unknown: /" + cmd + ". Try /help"});
    return true;
}

} // namespace tui
} // namespace ai
