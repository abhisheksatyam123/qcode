#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <ai/file_logger.h>
#include <ai/tui/chat.h>
#include <ai/tui/commands.h>
#include <ai/tui/config.h>
#include <ai/tui/db.h>
#include <ai/tui/state.h>
#include <ai/tui/system_prompt.h>
#include <ai/tui/views.h>

using namespace ftxui;

int main() {
    ai::install_file_logger("/tmp/qcode.log", ai::logger::LogLevel::kLogLevelDebug);
    ai::logger::log_info("QCode starting...");

    auto screen = ScreenInteractive::Fullscreen();

    ai::tui::ChatState state;
    std::string prompt_input;

    ai::tui::ToolConfig tool_cfg{true, true};
    std::string system_prompt = ai::tui::SystemPrompt::build_default(tool_cfg);
    bool enable_tools = true;

    // ── Providers ──
    auto providers_list = ai::tui::load_providers_from_config();
    int selected_provider = 0;
    int selected_model = 0;

    // ── SQLite Persistence Initialization ──
    ai::tui::db::init_database();
    std::string last_session = ai::tui::db::get_last_active_session();
    if (last_session.empty() && !providers_list.empty()) {
        std::string prov = providers_list[selected_provider].name;
        std::string mod = providers_list[selected_provider].models[selected_model].name;
        last_session = ai::tui::db::create_new_session(prov, mod);
    }
    *state.session_id = last_session;
    if (!state.session_id->empty()) {
        ai::tui::db::reload_session_history(*state.session_id, state);
    }

    // Populate modified files at start
    ai::tui::update_modified_files(state);

    // ── Model selector popup ──
    bool show_model_select = false;
    int model_select_idx = 0;
    auto model_entries = ai::tui::build_model_entries(providers_list);

    // ── Slash command popup ──
    bool show_slash = false;
    int slash_idx = 0;
    auto slash_commands = ai::tui::builtin_slash_commands();

    // ── TUI Components ──
    std::vector<std::string> tab_values = {"Chat", "Files", "Stats"};
    Component tab_toggle = Toggle(&tab_values, &state.tab_selected);

    Component files_menu = Menu(state.modified_files.get(), &state.selected_file);

    Component input = Input(&prompt_input, "Type a message or / for commands...");

    // ── Keyboard event handling ──
    input |= CatchEvent([&](Event e) {
        if (show_model_select) {
            if (e == Event::ArrowDown || e == Event::Character('j')) {
                model_select_idx = (model_select_idx + 1) %
                                   static_cast<int>(model_entries.size());
                return true;
            }
            if (e == Event::ArrowUp || e == Event::Character('k')) {
                model_select_idx = (model_select_idx - 1 +
                                    static_cast<int>(model_entries.size())) %
                                   static_cast<int>(model_entries.size());
                return true;
            }
            if (e == Event::Return) {
                auto& entry = model_entries[static_cast<size_t>(model_select_idx)];
                selected_provider = entry.provider_idx;
                selected_model = entry.model_idx;
                show_model_select = false;
                return true;
            }
            if (e == Event::Escape || e == Event::Backspace) {
                show_model_select = false;
                return true;
            }
            if (e.is_character()) {
                char ch = std::tolower(e.character()[0]);
                for (int i = 1; i <= static_cast<int>(model_entries.size()); i++) {
                    int idx = (model_select_idx + i) %
                              static_cast<int>(model_entries.size());
                    auto& name = model_entries[idx].model_name;
                    if (!name.empty() && std::tolower(name[0]) == ch) {
                        model_select_idx = idx;
                        return true;
                    }
                }
                return true;
            }
            return true;
        }

        if (show_slash) {
            if (e == Event::ArrowDown || e == Event::Character('j')) {
                slash_idx = (slash_idx + 1) %
                            static_cast<int>(slash_commands.size());
                return true;
            }
            if (e == Event::ArrowUp || e == Event::Character('k')) {
                slash_idx = (slash_idx - 1 + static_cast<int>(slash_commands.size())) %
                            static_cast<int>(slash_commands.size());
                return true;
            }
            if (e == Event::Return) {
                show_slash = false;
                auto& cmd = slash_commands[static_cast<size_t>(slash_idx)];
                if (cmd.name == "model") {
                    show_model_select = true;
                    model_select_idx = 0;
                    for (int i = 0; i < static_cast<int>(model_entries.size()); i++) {
                        if (model_entries[i].provider_idx == selected_provider &&
                            model_entries[i].model_idx == selected_model) {
                            model_select_idx = i;
                            break;
                        }
                    }
                } else {
                    prompt_input = "/" + cmd.name + " ";
                }
                return true;
            }
            if (e == Event::Escape || e == Event::Backspace) {
                show_slash = false;
                return true;
            }
            if (e.is_character()) {
                char ch = std::tolower(e.character()[0]);
                for (int i = 1; i <= static_cast<int>(slash_commands.size()); i++) {
                    int idx = (slash_idx + i) % static_cast<int>(slash_commands.size());
                    if (!slash_commands[idx].name.empty() &&
                        std::tolower(slash_commands[idx].name[0]) == ch) {
                        slash_idx = idx;
                        return true;
                    }
                }
                return true;
            }
            return true;
        }

        // Open slash popup when typing "/" at start of empty input
        if (e.is_character() && e.character() == "/" && prompt_input.empty()) {
            show_slash = true;
            slash_idx = 0;
            return true;
        }

        // ── Visual Message Selection Mode (for Copy) ──
        // Toggle selection mode via Ctrl+P
        if (e == Event::Special("\x10")) { // Ctrl+P
            if (!state.chat_history->empty()) {
                state.selection_mode = !state.selection_mode;
                state.selected_message = state.selection_mode ? 
                    static_cast<int>(state.chat_history->size()) - 1 : -1;
            }
            return true;
        }

        if (state.selection_mode) {
            if (e == Event::ArrowUp || e == Event::Character('k')) {
                if (state.selected_message > 0) state.selected_message--;
                return true;
            }
            if (e == Event::ArrowDown || e == Event::Character('j')) {
                if (state.selected_message < static_cast<int>(state.chat_history->size()) - 1)
                    state.selected_message++;
                return true;
            }
            if (e == Event::Return || e == Event::Character('y')) {
                if (state.selected_message >= 0 && 
                    state.selected_message < static_cast<int>(state.chat_history->size())) {
                    auto& msg = (*state.chat_history)[state.selected_message];
                    ai::tui::copy_to_clipboard(msg.second);
                    state.chat_history->push_back({"System", "Yanked message to clipboard!"});
                    ai::tui::db::save_message(*state.session_id, "System", "Yanked message to clipboard!");
                }
                state.selection_mode = false;
                state.selected_message = -1;
                return true;
            }
            if (e == Event::Escape) {
                state.selection_mode = false;
                state.selected_message = -1;
                return true;
            }
            return true; // Consume keys in selection mode
        }

        return false;
    });

    // ── Submit handler ──
    auto submit = [&] {
        if (prompt_input.empty() || *state.is_generating) return;

        // Slash commands
        if (prompt_input[0] == '/') {
            std::string raw = prompt_input;
            prompt_input = "";
            ai::tui::handle_slash_command(raw, prompt_input, providers_list,
                                          selected_provider, selected_model,
                                          enable_tools, system_prompt, state);
            return;
        }

        // Normal message
        *state.is_generating = true;
        std::string p = prompt_input;
        prompt_input = "";
        state.chat_history->push_back({"User", p});
        state.messages_history->push_back(ai::Message::user(p));

        // Save User Message to SQLite
        ai::tui::db::save_message(*state.session_id, "User", p);

        std::thread([&, p] {
            ai::tui::run_llm_generation(
                providers_list[selected_provider].name,
                providers_list[selected_provider].models[selected_model].id,
                system_prompt, *state.messages_history, enable_tools,
                providers_list, &screen, state);
            // Refresh modified files list on completion
            ai::tui::update_modified_files(state);
            screen.Post(Event::Custom);
        }).detach();
    };

    input |= CatchEvent([&](Event e) {
        if (e == Event::Return && !show_slash && !show_model_select && !prompt_input.empty()) {
            submit();
            return true;
        }
        return false;
    });

    // ── Layout Tree & Focus navigation ──
    auto main_container = Container::Vertical({
        tab_toggle,
        files_menu,
        input
    });

    // Handle cycling focus or keyboard tabs
    main_container |= CatchEvent([&](Event e) {
        // Tab navigation: Cycle active focus between tab toggle, files menu, or input
        if (e == Event::Tab) {
            if (state.tab_selected == 1) { // Files tab
                if (tab_toggle->Focused()) {
                    files_menu->TakeFocus();
                } else {
                    tab_toggle->TakeFocus();
                }
            } else if (state.tab_selected == 0) { // Chat tab
                if (tab_toggle->Focused()) {
                    input->TakeFocus();
                } else {
                    tab_toggle->TakeFocus();
                }
            }
            return true;
        }
        
        // Tab hotkeys (Alt+1, Alt+2, Alt+3)
        if (e == Event::Special("\x1b\x31")) { state.tab_selected = 0; return true; }
        if (e == Event::Special("\x1b\x32")) { state.tab_selected = 1; return true; }
        if (e == Event::Special("\x1b\x33")) { state.tab_selected = 2; return true; }
        
        return false;
    });

    auto renderer = Renderer(main_container, [&] {
        return ai::tui::render_view(
            state, providers_list, selected_provider, selected_model,
            enable_tools,
            show_slash, slash_idx, slash_commands,
            show_model_select, model_select_idx, model_entries,
            tab_toggle, files_menu, input);
    });

    screen.Loop(renderer);
}
