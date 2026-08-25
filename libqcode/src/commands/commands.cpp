#include <qcode/ui/commands.h>
#include <qcode/providers/provider_profile.h>
#include <qcode/providers/provider_transform.h>
#include <qcode/session/session_store.h>
#include <qcode/core/config.h>
#include <qcode/core/event.h>
#include <qcode/ui/themes.h>
#include <qcode/core/logger.h>
#include <qcode/core/client.h>
#include <qcode/providers/registry.h>
#include <qcode/providers/authenticated_providers.h>
#include <memory>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <thread>
#include <ctime>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <utility>
#include <atomic>

namespace qcode {

namespace {
void append_system_message(ChatState& state, std::string text) {
    state.messages_history->emplace_back(qcode::Message::system(std::move(text)));
}
}  // namespace

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

std::vector<VariantEntry> build_variant_entries(const ModelInfo& model) {
    std::vector<VariantEntry> entries;
    entries.push_back({"off", "Off", "No extra thinking tokens"});
    auto efforts = ProviderTransform::reasoning_variants(model);
    if (efforts.empty()) {
        efforts = {"low", "medium", "high", "max"};
    }
    const auto describe = [](const std::string& id) -> std::string {
        if (id == "low") return "Fast, light reasoning";
        if (id == "medium") return "Balanced thinking";
        if (id == "high") return "Deep reasoning";
        if (id == "max") return "Maximum thinking budget";
        return "Model-specific effort";
    };
    for (const auto& id : efforts) {
        if (id.empty() || id == "off") continue;
        std::string title = id;
        if (!title.empty()) title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(title[0])));
        entries.push_back({id, title, describe(id)});
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
    ChatState& state,
    std::shared_ptr<qcode::compat::jthread> compaction_thread,
    bus::BusPort& bus
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
        append_system_message(state, m.str());
        return true;
    }

    if (cmd == "theme" || cmd == "themes") {
        LOG_DEBUG("Commands: /theme args='{}'", args);
        if (args.empty()) {
            std::string cur = state.theme ? *state.theme : "orange";
            std::ostringstream list;
            list << "Available themes:\n";
            for (const auto& entry : builtin_theme_entries()) {
                list << "  /theme " << entry.name << " (" << entry.description
                     << ")\n";
            }
            list << "\nCurrent theme: " << cur;
            append_system_message(state, list.str());
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

        if (is_known_theme(new_theme)) {
            if (state.theme) {
                *state.theme = new_theme;
            }
            append_system_message(state, "Theme changed to: " + new_theme);
        } else {
            append_system_message(
                state,
                "Unknown theme: '" + new_theme +
                    "'. Use /theme to list supported themes.");
        }
        return true;
    }

    if (cmd == "new") {
        std::string prov = providers_list[selected_provider].name;
        std::string mod = providers_list[selected_provider].models[selected_model].name;
        
        std::string ws;
        std::string custom_id;
        
        std::string trimmed_args = args;
        trimmed_args.erase(trimmed_args.begin(), std::find_if(trimmed_args.begin(), trimmed_args.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        trimmed_args.erase(std::find_if(trimmed_args.rbegin(), trimmed_args.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), trimmed_args.end());

        if (!trimmed_args.empty()) {
            size_t last_space = trimmed_args.find_last_of(" 	");
            if (last_space != std::string::npos) {
                std::string last_word = trimmed_args.substr(last_space + 1);
                bool looks_like_path = false;
                if (!last_word.empty()) {
                    if (last_word[0] == '/' || last_word[0] == '.' || last_word[0] == '~') {
                        looks_like_path = true;
                    } else {
                        std::error_code ec;
                        if (std::filesystem::is_directory(last_word, ec)) {
                            looks_like_path = true;
                        }
                    }
                }
                if (looks_like_path) {
                    ws = last_word;
                    custom_id = trimmed_args.substr(0, last_space);
                    custom_id.erase(std::find_if(custom_id.rbegin(), custom_id.rend(), [](unsigned char ch) {
                        return !std::isspace(ch);
                    }).base(), custom_id.end());
                } else {
                    custom_id = trimmed_args;
                }
            } else {
                bool looks_like_path = false;
                if (trimmed_args[0] == '/' || trimmed_args[0] == '.' || trimmed_args[0] == '~') {
                    looks_like_path = true;
                } else {
                    std::error_code ec;
                    if (std::filesystem::is_directory(trimmed_args, ec)) {
                        looks_like_path = true;
                    }
                }
                if (looks_like_path) {
                    ws = trimmed_args;
                } else {
                    custom_id = trimmed_args;
                }
            }
        }

        std::string new_id = session::create_new_session(prov, mod, ws, custom_id);
        *state.session_id = new_id;
        state.messages_history->clear();
        std::string display_title = session::get_session_title(new_id);
        if (display_title.empty()) {
            display_title = custom_id.empty() ? ("Session - " + mod) : custom_id;
        }
        if (state.session_title) *state.session_title = display_title;
        if (state.retry_available) *state.retry_available = false;
        if (state.last_user_prompt) state.last_user_prompt->clear();
        bus.publish<qcode::contract::ToastRequested>({
            "Started new session: " + display_title + (!ws.empty() ? (" (" + ws + ")") : ""),
            "success",
            2500
        });
        return true;
    }

    if (cmd == "rename") {
        std::string new_title = args;
        new_title.erase(new_title.begin(), std::find_if(new_title.begin(), new_title.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        new_title.erase(std::find_if(new_title.rbegin(), new_title.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), new_title.end());

        if (new_title.empty()) {
            append_system_message(state, "Usage: /rename <new name>");
            return true;
        }
        if (state.session_id->empty()) {
            append_system_message(state, "No active session to rename. Start or load one first.");
            return true;
        }
        session::rename_session(*state.session_id, new_title);
        if (state.session_title) *state.session_title = new_title;
        append_system_message(state, "Renamed session to: " + new_title);
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
            append_system_message(state, "Usage: /session <session_id>");
            return true;
        }

        if (!session::is_valid_session_id(target_id)) {
            append_system_message(state, "Invalid session id: " + target_id);
            return true;
        }

        auto list = session::list_sessions();
        bool found = false;
        for (auto& s : list) {
            if (s.first == target_id) {
                found = true;
                break;
            }
        }

        if (found) {
            *state.session_id = target_id;
            session::reload_session_history(target_id, state);
            
            // Restore provider and model from the loaded session!
            std::string loaded_prov_id;
            std::string loaded_model_id;
            for (const auto& s : session::list_sessions_full()) {
                if (s.id == target_id) {
                    loaded_prov_id = s.provider;
                    loaded_model_id = s.model;
                    break;
                }
            }
            if (!loaded_prov_id.empty() && !loaded_model_id.empty()) {
                for (int i = 0; i < static_cast<int>(providers_list.size()); ++i) {
                    if (providers_list[i].name == loaded_prov_id || providers_list[i].id == loaded_prov_id) {
                        for (int j = 0; j < static_cast<int>(providers_list[i].models.size()); ++j) {
                            if (providers_list[i].models[j].name == loaded_model_id || providers_list[i].models[j].id == loaded_model_id) {
                                selected_provider = i;
                                selected_model = j;
                                break;
                            }
                        }
                        break;
                    }
                }
            }
            append_system_message(state, "Loaded persistent session: " + target_id);
        } else {
            append_system_message(state, "Session ID not found: " + target_id);
        }
        return true;
    }

    if (cmd == "agent") {
        // /agent          — show current agent
        // /agent <name>   — switch (build|plan)
        std::string name = args;
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(name.begin());
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
        if (name.empty()) {
            const std::string cur = state.agent_mode ? *state.agent_mode : "build";
            bus.publish<qcode::contract::ToastRequested>({
                .message = "Agent: " + cur + " (/agent build|plan)",
                .variant = "info"
            });
            return true;
        }
        if (name != "build" && name != "plan") {
            bus.publish<qcode::contract::ToastRequested>({
                .message = "Unknown agent '" + name + "'. Use build|plan.",
                .variant = "warning"
            });
            return true;
        }
        *state.agent_mode = name;
        if (state.session_id && !state.session_id->empty()) {
            qcode::session::set_session_modes(
                *state.session_id, name,
                state.reasoning_mode ? *state.reasoning_mode : "off");
        }
        bus.publish<qcode::contract::ToastRequested>({
            .message = name == "plan"
                           ? "Plan mode: read-only research, no edits"
                           : "Build mode: full tool access",
            .variant = "success"
        });
        LOG_INFO("Commands: agent mode set to '{}'", name);
        return true;
    }

    if (cmd == "variant") {
        std::string lvl = args;
        // Trim whitespace
        while (!lvl.empty() && std::isspace(static_cast<unsigned char>(lvl.front()))) lvl.erase(lvl.begin());
        while (!lvl.empty() && std::isspace(static_cast<unsigned char>(lvl.back()))) lvl.pop_back();
        if (lvl.empty()) {
            // TUI intercepts bare /variant and opens the picker. Keep a
            // cycle here for non-TUI callers (CLI / server).
            std::string cur = state.reasoning_mode ? *state.reasoning_mode : "off";
            if (cur == "off" || cur.empty()) lvl = "low";
            else if (cur == "low") lvl = "medium";
            else if (cur == "medium") lvl = "high";
            else if (cur == "high") lvl = "max";
            else lvl = "off";
        }
        if (lvl != "off" && lvl != "low" && lvl != "medium" && lvl != "high" && lvl != "max") {
            bus.publish<qcode::contract::ToastRequested>({
                .message = "Invalid variant '" + lvl + "'. Use off|low|medium|high|max.",
                .variant = "warning"
            });
            return true;
        }
        *state.reasoning_mode = lvl;
        if (state.session_id && !state.session_id->empty()) {
            qcode::session::set_session_modes(
                *state.session_id,
                state.agent_mode ? *state.agent_mode : "build", lvl);
        }
        bus.publish<qcode::contract::ToastRequested>({
            .message = "Model variant (effort): " + lvl,
            .variant = (lvl == "off" ? "info" : "success")
        });
        LOG_DEBUG("Commands: variant set to '{}'", lvl);
        return true;
    }

    if (cmd == "retry") {
        std::string prompt;
        if (state.last_user_prompt && !state.last_user_prompt->empty()) {
            prompt = *state.last_user_prompt;
        } else if (state.messages_history) {
            for (auto it = state.messages_history->rbegin(); it != state.messages_history->rend(); ++it) {
                if (it->role == kMessageRoleUser && !it->has_tool_results()) {
                    prompt = it->get_text();
                    if (!prompt.empty()) break;
                }
            }
        }
        if (prompt.empty()) {
            bus.publish<qcode::contract::ToastRequested>({
                "No user prompt available to retry.",
                "warning",
                2000
            });
            return true;
        }
        if (state.last_user_prompt) *state.last_user_prompt = prompt;
        if (state.retry_available) *state.retry_available = true;
        bus.publish<qcode::contract::ToastRequested>({
            "Prompt ready for retry (press r to resend)",
            "info",
            2000
        });
        return true;
    }

    if (cmd == "compact") {
        LOG_DEBUG("Commands: /compact args='{}'", args);
        int keep = 4;
        if (!args.empty()) {
            try {
                keep = std::max(0, std::stoi(args));
            } catch (...) {
                append_system_message(
                    state, "Usage: /compact [keep]  (keep = recent messages to retain)");
                return true;
            }
        }
        run_compaction(state, providers_list, selected_provider, selected_model,
                       keep, compaction_thread, bus);
        return true;
    }

    if (cmd == "tools") {
        LOG_DEBUG("Commands: /tools args='{}'", args);
        if (args.empty()) {
            enable_tools = !enable_tools;
            std::string status = enable_tools ? "ENABLED (observability mode)" : "DISABLED (real-time streaming mode)";
            append_system_message(state, "Tools " + status);
        } else if (args == "on") {
            enable_tools = true;
            append_system_message(state, "Tools ENABLED (observability mode)");
        } else if (args == "off") {
            enable_tools = false;
            append_system_message(state, "Tools DISABLED (real-time streaming mode)");
        } else {
            append_system_message(state, "Usage: /tools [on|off]");
        }
        return true;
    }

    if (cmd == "help" || cmd == "?") {
        std::ostringstream h;
        h << "Available commands:\n"
          << "  /model [list]     - select provider/model\n"
          << "  /agent [build|plan] - switch agent mode (plan = read-only research)\n"
          << "  /variant [off|low|medium|high|max] - open variant picker (or set effort)\n"
          << "  /theme [name]     - set UI theme (classic + pastel: mint/sky/rose/...)\n"
          << "  /new [name] [workspace] - new session (optional title + workspace path)\n"
          << "  /session <id>     - load a persistent session by id\n"
          << "  /rename <name>    - rename the current session title\n"
          << "  /compact [keep]   - compress conversation into a handoff summary\n"
          << "  /tools [on|off]   - toggle tool use (observability vs streaming mode)\n"
          << "  /queue [rm <n>]   - list queued prompts / remove one (/cq clears all)\n"
          << "  /help             - show this help\n"
          << "Keys: Esc stop · r retry last failed turn · F3 copy mode\n"
          << "      Click Thought headers to expand · Ctrl-P plan mode\n"
          << "Bash tool modes: run, background, list, status, kill, remove, cleanup.";
        append_system_message(state, h.str());
        return true;
    }
    append_system_message(state, "Unknown command: /" + cmd);
    return true;
}

void run_compaction(
    ChatState& state,
    const std::vector<ProviderInfo>& providers_list,
    int selected_provider,
    int selected_model,
    int keep,
    std::shared_ptr<qcode::compat::jthread> compaction_thread,
    bus::BusPort& bus
) {
    static std::atomic<bool> compaction_busy{false};

    const auto& hist = *state.messages_history;
    if (hist.size() <= 2) {
        append_system_message(
            state, "Nothing to compact: conversation is too short.");
        return;
    }

    if (compaction_busy.exchange(true)) {
        append_system_message(state, "Compaction already in progress.");
        bus.publish<qcode::contract::ToastRequested>({
            .message = "Compaction already in progress",
            .variant = "warning",
            .duration_ms = 2500});
        return;
    }

    // Snapshot the conversation for the worker thread.
    qcode::Messages snapshot = hist;
    auto providers_copy = providers_list;
    int sp = selected_provider;
    int sm = selected_model;
    std::string sid = *state.session_id;

    bus.publish<qcode::contract::ToastRequested>({
        .message = "Compacting conversation...",
        .variant = "info",
        .duration_ms = 4000});
    bus.publish<qcode::contract::SessionStatusChanged>({
        .session_id = sid,
        .status = "generating"});

    // Reap a finished prior worker without blocking on an in-flight one
    // (busy flag prevents overlap).
    if (compaction_thread->joinable()) {
        compaction_thread->request_stop();
        compaction_thread->join();
    }
    *compaction_thread = qcode::compat::jthread(
        [providers_copy, sp, sm, snapshot, keep, sid,
         &bus](qcode::compat::stop_token stop_token) mutable {
        qcode::contract::CompactionResult::Payload result;
        result.keep = keep;
        result.original_size = snapshot.size();

        auto finish_busy = []() { compaction_busy.store(false); };

        auto fail = [&](const std::string& msg) {
            result.error = msg;
            bus.publish<qcode::contract::CompactionResult>(result);
            finish_busy();
        };

        try {
        if (stop_token.stop_requested()) {
            fail("Compaction cancelled");
            return;
        }
        if (sp < 0 || sp >= static_cast<int>(providers_copy.size())) {
            fail("Invalid provider selection");
            return;
        }
        if (sm < 0 || sm >= static_cast<int>(providers_copy[sp].models.size())) {
            fail("Invalid model selection");
            return;
        }

        std::ostringstream transcript;
        for (const auto& m : snapshot) {
            std::string role_str;
            if (m.role == qcode::kMessageRoleUser) role_str = "User";
            else if (m.role == qcode::kMessageRoleAssistant) role_str = "Assistant";
            else if (m.role == qcode::kMessageRoleSystem) role_str = "System";
            else role_str = "Message";
            std::string text;
            for (const auto& part : m.content) {
                if (const auto* tp = std::get_if<qcode::TextContentPart>(&part)) {
                    text += tp->text + "\n";
                } else if (const auto* tcp = std::get_if<qcode::ToolCallContentPart>(&part)) {
                    text += "[Tool call: " + tcp->tool_name + "]\n";
                } else if (std::holds_alternative<qcode::ToolResultContentPart>(part)) {
                    text += "[Tool result]\n";
                } else if (const auto* rcp = std::get_if<qcode::ReasoningContentPart>(&part)) {
                    if (!rcp->text.empty()) text += "[Reasoning: " + rcp->text + "]\n";
                }
            }
            transcript << role_str << ": " << text << "\n";
        }

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

        const auto& sel = providers_copy[sp];
        const auto& model_id = providers_copy[sp].models[sm].id;
        qcode::providers::register_authenticated_providers();
        qcode::providers::ProviderOptions provider_options;
        provider_options.base_url = sel.api_url;
        provider_options.api_key = sel.api_key;
        provider_options.headers = sel.headers;
        provider_options.protocol = sel.protocol;
        provider_options.project_id = sel.project_id;
        const auto call =
            prepare_provider_call(provider_options, sel.id, model_id);
        const auto& wire_model = call.wire_model_id;
        auto resolution = qcode::providers::ProviderRegistry::instance().resolve(
            sel.id, provider_options);
        if (!resolution.ok()) {
            fail("Compaction failed: " + resolution.error);
            return;
        }
        if (stop_token.stop_requested()) {
            fail("Compaction cancelled");
            return;
        }
        qcode::Client client = std::move(resolution.client);

        qcode::GenerateOptions opts;
        opts.model = wire_model;
        opts.system = compaction_instruction;
        opts.messages = {qcode::Message::user(transcript.str())};

        qcode::GenerateResult res = client.generate_text(opts);
        if (stop_token.stop_requested()) {
            fail("Compaction cancelled");
            return;
        }
        if (!res.is_success() || (res.error && !res.error->empty())) {
            fail("Compaction failed: " +
                 (res.error && !res.error->empty() ? *res.error
                                                   : res.error_message()));
            return;
        }
        std::string summary = res.text;
        if (summary.empty()) {
            fail("Compaction failed: empty summary from model.");
            return;
        }

        std::string notes_root = qcode::get_notes_root();

        std::error_code ec;
        std::filesystem::create_directories(
            notes_root + "/scratchpad/task/qcode-tui/active", ec);
        std::string todo_path = notes_root +
                                "/scratchpad/task/qcode-tui/active/todo-" + sid + ".md";
        bool wrote = false;
        if (!ec) {
            std::ofstream out(todo_path);
            if (out) {
                out << "# qcode compacted handoff\n\n" << summary << "\n";
                wrote = true;
            }
        }

        result.summary = std::move(summary);
        result.todo_path = std::move(todo_path);
        result.wrote = wrote;
        bus.publish<qcode::contract::CompactionResult>(result);
        finish_busy();
        } catch (const std::exception& e) {
            fail(std::string("Compaction failed: ") + e.what());
        } catch (...) {
            fail("Compaction failed: unknown error");
        }
        });
}
} // namespace qcode
