#include <fstream>
#include <sstream>
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
#include <climits>

#include <ai/file_logger.h>
#include <ai/tui/chat_bus.h>
#include <ai/tui/provider_registry_init.h>
#include <ai/tui/commands.h>
#include <ai/tui/config.h>
#include <ai/tui/db.h>
#include <ai/tui/state.h>
#include <ai/tui/system_prompt.h>
#include <ai/tui/views.h>
#include <ai/tui/store.h>
#include <ai/tui/bus/impl.h>
#include <ai/tui/contract/event.h>
#include <ai/tui/contract/identity.h>
#include <atomic>
#include <chrono>
#include <functional>

using namespace ftxui;
using namespace ai::tui::contract;

static bool matches_query(const std::string& target, const std::string& query) {
    if (query.empty()) return true;
    auto it = std::search(
        target.begin(), target.end(),
        query.begin(), query.end(),
        [](unsigned char ch1, unsigned char ch2) {
            return std::tolower(ch1) == std::tolower(ch2);
        }
    );
    return it != target.end();
}

int main() {
    ai::install_file_logger("/tmp/qcode.log", ai::logger::LogLevel::kLogLevelDebug);
    ai::logger::set_thread_name("main");
    LOG_INFO("QCode starting (bus architecture)...");

    auto app_running = std::make_shared<std::atomic<bool>>(true);
    auto background_threads = std::make_shared<std::vector<std::thread>>();
    auto screen = ScreenInteractive::Fullscreen();

    // ═══════════════════════════════════════════════════════════
    //  1. Initialize the message bus + store
    // ═══════════════════════════════════════════════════════════
    auto bus = std::make_shared<ai::tui::bus::BusRuntime>();
    register_all_events(*bus);
    // Populate the provider registry once, before any generation thread starts,
    // so resolve() only ever reads a fully-initialized registry.
    ai::providers::register_tui_providers();
    ai::tui::AppStore store(*bus);

    // Wire bus events to store mutations
    store.wire();

    // ── Legacy state access ──
    auto& state = store.state();

    // ── Spinner: advance frame periodically ──
    std::thread spinner_thread([&store, app_running]() {
        ai::logger::set_thread_name("spinner");
        while (app_running->load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            if (store.is_generating()) {
                store.advance_frame();
            }
        }
    });
    background_threads->push_back(std::move(spinner_thread));



    // ═══════════════════════════════════════════════════════════
    //  2. Provider & Config setup
    // ═══════════════════════════════════════════════════════════
    std::string prompt_input;
    ai::tui::ToolConfig tool_cfg{true, true};
    std::string system_prompt = ai::tui::SystemPrompt::build_default(tool_cfg);
    bool enable_tools = true;

    auto providers_list = ai::tui::load_providers_from_config();
    int selected_provider = 0;
    int selected_model = 0;

    ai::tui::db::init_database();
    std::string last_session = ai::tui::db::get_last_active_session();
    if (last_session.empty() && !providers_list.empty()) {
        std::string prov = providers_list[selected_provider].name;
        std::string mod = providers_list[selected_provider].models[selected_model].name;
        last_session = ai::tui::db::create_new_session(prov, mod);
    }
    store.set_session_id(last_session);
    if (!store.session_id().empty()) {
        ai::tui::db::reload_session_history(store.session_id(), state);
    }
    ai::tui::update_modified_files(state);

    // ── Popups state ──
    bool show_model_select = false;
    int model_select_idx = 0;
    std::string model_query = "";
    auto model_entries = ai::tui::build_model_entries(providers_list);

    bool show_session_select = false;
    int session_select_idx = 0;
    std::string session_query = "";
    std::vector<ai::tui::db::SessionInfo> session_entries;

    bool show_theme_select = false;
    int theme_select_idx = 0;
    std::string theme_query = "";
    std::vector<ai::tui::ThemeEntry> theme_entries = {
        {"orange", "Classic Orange"},
        {"green", "Forest Green"},
        {"blue", "Deep Blue"},
        {"purple", "Cyberpunk Purple"},
        {"monochrome", "Monochrome"}
    };

    bool show_slash = false;
    int slash_idx = 0;
    auto slash_commands = ai::tui::builtin_slash_commands();

    // ── TUI Components ──
    std::vector<std::string> tab_values = {"Chat", "Files", "Stats"};
    Component tab_toggle = Toggle(&tab_values, &state.tab_selected);

    Component files_menu = Menu(state.modified_files.get(), &state.selected_file);

    InputOption input_opts = InputOption::Default();
    input_opts.multiline = true;
    input_opts.transform = [](InputState s) {
        s.element = s.element | bgcolor(Color::Default);
        if (s.focused) {
            s.element = s.element | bgcolor(Color::Default);
        }
        return s.element;
    };
    Component input = Input(&prompt_input, "Type a message or / for commands...", input_opts);

    // ═══════════════════════════════════════════════════════════
    //  3. Submit handler (uses bus-aware chat)
    // ═══════════════════════════════════════════════════════════
    // ── Generation spawner + queue drain ──
    // Spawns an LLM generation thread for a single prompt. When the generation
    // finishes, it automatically drains the next queued prompt (if any) so a
    // sequence of queued prompts runs back-to-back. Queued prompts keep running
    // even after an error (the error stays visible as a System message + toast).
    std::function<void(const std::string&)> spawn_generation;
    spawn_generation = [&](const std::string& p) {
        store.set_generating(true);
        *state.auto_scroll = true;
        store.append_chat_message("User", p);
        ai::tui::db::save_message(store.session_id(), "User", p);
        LOG_INFO("Main: spawn_generation prompt_len={} queue_remaining={} provider={} model={}",
                 p.size(), store.queue_size(),
                 providers_list[selected_provider].name,
                 providers_list[selected_provider].models[selected_model].name);

        // Capture by value so the worker thread is independent of the UI thread.
        auto providers_copy = providers_list;
        int sel_prov = selected_provider;
        int sel_mod = selected_model;
        std::string sys_prompt = system_prompt;
        bool tools_enabled = enable_tools;
        auto bus_ptr = bus;
        auto state_ptr = &state;

        std::thread llm_thread([bus_ptr, state_ptr, p, providers_copy, app_running,
                                sel_prov, sel_mod, sys_prompt, tools_enabled,
                                &store, &spawn_generation]() {
            ai::logger::set_thread_name("llm");
            auto gen_start = std::chrono::steady_clock::now();
            ai::tui::BackendService backend{*bus_ptr, providers_copy};
            ai::tui::GenerationContext ctx{
                .session_id = *state_ptr->session_id,
                .reasoning_mode = *state_ptr->reasoning_mode
            };
            backend.run_generation(
                providers_copy[sel_prov].id,
                providers_copy[sel_prov].models[sel_mod].id,
                sys_prompt, *state_ptr->messages_history, tools_enabled,
                ctx);
            // Sync counters back to ChatState for stats tab
            *state_ptr->tool_call_count = ctx.tool_call_count;
            *state_ptr->total_tool_time_ms = ctx.total_tool_time_ms;

            auto gen_end = std::chrono::steady_clock::now();
            auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(gen_end - gen_start).count();
            LOG_INFO("Main: generation complete duration_ms={} queue_remaining={}", dur_ms, store.queue_size());

            if (app_running->load()) {
                ai::tui::update_modified_files(*state_ptr);
            }

            // Drain the queue: run the next queued prompt (if any).
            if (store.has_queued_prompt()) {
                std::string next = store.dequeue_prompt();
                int remaining = static_cast<int>(store.queue_size());
                LOG_INFO("Main: draining queued prompt (remaining={})", remaining);
                store.add_toast("Running queued prompt (" +
                                std::to_string(remaining) + " left)", "info", 1500);
                spawn_generation(next);
            }
        });
        background_threads->push_back(std::move(llm_thread));
    };

    //  3. Submit handler (uses bus-aware chat)
    // ═══════════════════════════════════════════════════════════
    auto submit = [&] {
        if (prompt_input.empty()) return;
        if (store.is_generating()) {
            if (store.has_queued_prompt()) {
                store.append_to_last_queued_prompt(prompt_input);
                store.add_toast("Prompt appended to queue", "info", 1000);
                LOG_INFO("Main: prompt appended to queue (queue_size={})", store.queue_size());
            } else {
                store.enqueue_prompt(prompt_input);
                store.add_toast("Prompt queued", "info", 1000);
                LOG_INFO("Main: prompt queued (queue_size={})", store.queue_size());
            }
            prompt_input = "";
            return;
        }

        LOG_DEBUG("Main: submit prompt_len={}", prompt_input.size());
        // Slash commands
        if (prompt_input[0] == '/') {
            std::string raw = prompt_input;
            std::string cmd = raw.substr(1);
            cmd.erase(cmd.begin(), std::find_if(cmd.begin(), cmd.end(), [](unsigned char ch) {
                return !std::isspace(ch);
            }));
            cmd.erase(std::find_if(cmd.rbegin(), cmd.rend(), [](unsigned char ch) {
                return !std::isspace(ch);
            }).base(), cmd.end());

            if (cmd == "session" || cmd == "list") {
                prompt_input = "";
                session_entries = ai::tui::db::list_sessions_full();
                if (!session_entries.empty()) {
                    show_session_select = true;
                    session_select_idx = 0;
                    session_query = "";
                    for (int i = 0; i < static_cast<int>(session_entries.size()); i++) {
                        if (session_entries[i].id == store.session_id()) {
                            session_select_idx = i;
                            break;
                        }
                    }
                } else {
                    store.append_chat_message("System", "No saved sessions found.");
                }
                return;
            }

            if (cmd == "theme" || cmd == "themes") {
                prompt_input = "";
                show_theme_select = true;
                theme_select_idx = 0;
                theme_query = "";
                std::string cur = state.theme ? *state.theme : "orange";
                for (int i = 0; i < static_cast<int>(theme_entries.size()); i++) {
                    if (theme_entries[i].name == cur) {
                        theme_select_idx = i;
                        break;
                    }
                }
                return;
            }

            prompt_input = "";
            ai::tui::handle_slash_command(raw, prompt_input, providers_list,
                                          selected_provider, selected_model,
                                          enable_tools, system_prompt, state,
                                          background_threads, *bus);
            return;
        }

        // Normal message — hand off to the spawner (which also drains the queue)
        std::string p = prompt_input;
        prompt_input = "";
        spawn_generation(p);
    };


    // ═══════════════════════════════════════════════════════════
    //  4. Event handlers (keyboard, mouse, etc.)
    // ═══════════════════════════════════════════════════════════
    input |= CatchEvent([&](Event e) {
        // ── Model select popup ──
        if (show_model_select) {
            std::vector<ai::tui::ModelEntry> filtered_model_entries;
            for (const auto& entry : model_entries) {
                if (matches_query(entry.model_name, model_query) ||
                    matches_query(entry.model_id, model_query) ||
                    matches_query(entry.category, model_query)) {
                    filtered_model_entries.push_back(entry);
                }
            }

            if (e == Event::ArrowDown) {
                if (!filtered_model_entries.empty()) {
                    model_select_idx = std::min(model_select_idx + 1, (int)filtered_model_entries.size() - 1);
                }
                return true;
            }
            if (e == Event::ArrowUp) {
                if (!filtered_model_entries.empty()) {
                    model_select_idx = std::max(model_select_idx - 1, 0);
                }
                return true;
            }
            if (e == Event::Return) {
                if (!filtered_model_entries.empty()) {
                    auto& entry = filtered_model_entries[model_select_idx];
                    selected_provider = entry.provider_idx;
                    selected_model = entry.model_idx;
                    system_prompt = ai::tui::SystemPrompt::build_default(tool_cfg);
                }
                show_model_select = false;
                model_query = "";
                return true;
            }

            if (e == Event::Backspace || e == Event::Special("\x7f")) {
                if (!model_query.empty()) {
                    model_query.pop_back();
                    model_select_idx = 0;
                }
                return true;
            }
            if (e.is_character()) {
                model_query += e.character();
                model_select_idx = 0;
                return true;
            }
            return true;
        }

        // ── Session select popup ──
        if (show_session_select) {
            std::vector<ai::tui::db::SessionInfo> filtered_session_entries;
            for (const auto& entry : session_entries) {
                if (matches_query(entry.title, session_query) ||
                    matches_query(entry.id, session_query) ||
                    matches_query(entry.workspace, session_query)) {
                    filtered_session_entries.push_back(entry);
                }
            }

            if (e == Event::ArrowDown) {
                if (!filtered_session_entries.empty()) {
                    session_select_idx = std::min(session_select_idx + 1, (int)filtered_session_entries.size() - 1);
                }
                return true;
            }
            if (e == Event::ArrowUp) {
                if (!filtered_session_entries.empty()) {
                    session_select_idx = std::max(session_select_idx - 1, 0);
                }
                return true;
            }
            if (e == Event::Return) {
                if (!filtered_session_entries.empty()) {
                    std::string sid = filtered_session_entries[session_select_idx].id;
                    store.set_session_id(sid);
                    state.chat_history->clear();
                    state.messages_history->clear();
                    ai::tui::db::reload_session_history(sid, state);
                }
                show_session_select = false;
                session_query = "";
                return true;
            }

            if (e == Event::Special(std::string(1, '\x04'))) { // Ctrl-D
                if (!filtered_session_entries.empty()) {
                    std::string sid = filtered_session_entries[session_select_idx].id;
                    ai::tui::db::delete_session(sid);
                    session_entries = ai::tui::db::list_sessions_full();
                    filtered_session_entries.clear();
                    for (const auto& entry : session_entries) {
                        if (matches_query(entry.title, session_query) ||
                            matches_query(entry.id, session_query) ||
                            matches_query(entry.workspace, session_query)) {
                            filtered_session_entries.push_back(entry);
                        }
                    }
                    session_select_idx = std::max(0, std::min(session_select_idx, (int)filtered_session_entries.size() - 1));
                }
                return true;
            }
            if (e == Event::Backspace || e == Event::Special("\x7f")) {
                if (!session_query.empty()) {
                    session_query.pop_back();
                    session_select_idx = 0;
                }
                return true;
            }
            if (e.is_character()) {
                session_query += e.character();
                session_select_idx = 0;
                return true;
            }
            return true;
        }

        // ── Theme select popup ──
        if (show_theme_select) {
            std::vector<ai::tui::ThemeEntry> filtered_theme_entries;
            for (const auto& entry : theme_entries) {
                if (matches_query(entry.name, theme_query) ||
                    matches_query(entry.description, theme_query)) {
                    filtered_theme_entries.push_back(entry);
                }
            }

            if (e == Event::ArrowDown) {
                if (!filtered_theme_entries.empty()) {
                    theme_select_idx = std::min(theme_select_idx + 1, (int)filtered_theme_entries.size() - 1);
                }
                return true;
            }
            if (e == Event::ArrowUp) {
                if (!filtered_theme_entries.empty()) {
                    theme_select_idx = std::max(theme_select_idx - 1, 0);
                }
                return true;
            }
            if (e == Event::Return) {
                if (!filtered_theme_entries.empty()) {
                    std::string new_theme = filtered_theme_entries[theme_select_idx].name;
                    if (state.theme) {
                        *state.theme = new_theme;
                    }
                    store.append_chat_message("System", "Theme changed to: " + new_theme);
                }
                show_theme_select = false;
                theme_query = "";
                return true;
            }

            if (e == Event::Backspace || e == Event::Special("\x7f")) {
                if (!theme_query.empty()) {
                    theme_query.pop_back();
                    theme_select_idx = 0;
                }
                return true;
            }
            if (e.is_character()) {
                theme_query += e.character();
                theme_select_idx = 0;
                return true;
            }
            return true;
        }

        // ── Slash completion popup ──
        if (!prompt_input.empty() && prompt_input[0] == '/') {
            std::string partial = prompt_input.substr(1);
            std::vector<ai::tui::SlashCommand> command_matches;
            for (const auto& cmd : slash_commands) {
                if (cmd.name.find(partial) != std::string::npos) {
                    command_matches.push_back(cmd);
                }
            }
            int max_idx = static_cast<int>(command_matches.size()) - 1;

            if (command_matches.empty()) {
                state.slash_suggestion_mode = false;
                state.slash_suggestion_idx = 0;
            } else if (!state.slash_suggestion_mode) {
                if (command_matches.size() == 1 && command_matches[0].name == partial) {
                    // exact match — keep showing but don't force dropdown
                } else {
                    state.slash_suggestion_mode = true;
                    state.slash_suggestion_idx = 0;
                }
            }

            if (state.slash_suggestion_mode && !command_matches.empty()) {
                int active_idx = state.slash_suggestion_idx;
                if (active_idx < 0 || active_idx >= static_cast<int>(command_matches.size())) {
                    active_idx = 0;
                    state.slash_suggestion_idx = 0;
                }

                if (e == Event::Tab || e == Event::Return) {
                    if (e == Event::Tab) {
                        state.slash_suggestion_idx = (state.slash_suggestion_idx + 1) % command_matches.size();
                        return true;
                    }
                    if (e == Event::Return) {
                        std::string cmd_name = command_matches[active_idx].name;
                        prompt_input = "";
                        state.slash_suggestion_mode = false;
                        state.slash_suggestion_idx = 0;

                        if (cmd_name == "session" || cmd_name == "list") {
                            session_entries = ai::tui::db::list_sessions_full();
                            if (!session_entries.empty()) {
                                show_session_select = true;
                                session_select_idx = 0;
                                session_query = "";
                                for (int i = 0; i < static_cast<int>(session_entries.size()); i++) {
                                    if (session_entries[i].id == store.session_id()) {
                                        session_select_idx = i;
                                        break;
                                    }
                                }
                            } else {
                                store.append_chat_message("System", "No saved sessions found.");
                            }
                        } else if (cmd_name == "model") {
                            show_model_select = true;
                            model_select_idx = 0;
                            model_query = "";
                            for (int i = 0; i < static_cast<int>(model_entries.size()); i++) {
                                if (model_entries[i].provider_idx == selected_provider &&
                                    model_entries[i].model_idx == selected_model) {
                                    model_select_idx = i;
                                    break;
                                }
                            }
                        } else if (cmd_name == "theme") {
                            show_theme_select = true;
                            theme_select_idx = 0;
                            theme_query = "";
                            std::string cur = state.theme ? *state.theme : "orange";
                            for (int i = 0; i < static_cast<int>(theme_entries.size()); i++) {
                                if (theme_entries[i].name == cur) {
                                    theme_select_idx = i;
                                    break;
                                }
                            }
                        } else {
                            std::string raw = "/" + cmd_name;
                            ai::tui::handle_slash_command(raw, prompt_input, providers_list,
                                                          selected_provider, selected_model,
                                                          enable_tools, system_prompt, state,
                                                          background_threads, *bus);
                        }
                        return true;
                    }
                }

                if (e == Event::ArrowUp) {
                    state.slash_suggestion_mode = true;
                    state.slash_suggestion_idx = max_idx;
                    return true;
                }
                if (state.slash_suggestion_mode) {
                    if (e == Event::ArrowUp || e == Event::Character('k')) {
                        if (state.slash_suggestion_idx > 0) state.slash_suggestion_idx--;
                        else state.slash_suggestion_idx = max_idx;
                        return true;
                    }
                    if (e == Event::ArrowDown || e == Event::Character('j')) {
                        state.slash_suggestion_idx = (state.slash_suggestion_idx + 1) % (max_idx + 1);
                        return true;
                    }

                    if (e == Event::Backspace && prompt_input.size() <= 1) { state.slash_suggestion_mode = false; state.slash_suggestion_idx = 0; }
                }
            }
        }

        // Alt+Enter = newline
        if (e == Event::Special("\x1b\n") || e == Event::Special("\x1b\r") || e == Event::Special("\x1b\x0a")) {
            prompt_input += "\n";
            return true;
        }

        // Normal submit
        if (e == Event::Return && !prompt_input.empty()) {
            submit();
            return true;
        }

        return false;
    });

    // ═══════════════════════════════════════════════════════════
    //  5. Layout tree
    // ═══════════════════════════════════════════════════════════
    auto main_container = Container::Vertical({
        tab_toggle,
        files_menu,
        input
    });

    main_container |= CatchEvent([&](Event e) {
        // Toggle visibility of reasoning/thinking blocks (Ctrl-T or F2)
        if (e == Event::Special(std::string(1, '\x14')) || e == Event::F2) {
            *state.show_thinking = !*state.show_thinking;
            store.add_toast(*state.show_thinking ? "Thinking: shown"
                                                 : "Thinking: hidden",
                            "info", 2000);
            return true;
        }
        // Toggle COPY MODE (F3): disables mouse tracking so the terminal
        // emulator can do native text selection (clean copy/paste).
        if (e == Event::F3) {
            *state.copy_mode = !*state.copy_mode;
            screen.TrackMouse(!*state.copy_mode);
            if (*state.copy_mode) {
                store.add_toast("Copy mode ON - select text with mouse, press F3 to return",
                                "info", 4000);
            } else {
                store.add_toast("Copy mode OFF", "info", 1500);
            }
            screen.Post(Event::Custom);
            return true;
        }
        // ── Global Escape: close popups, dismiss suggestions, clear input ──
        if (e == Event::Escape) {
            if (show_model_select) { show_model_select = false; model_query = ""; return true; }
            if (show_session_select) { show_session_select = false; session_query = ""; return true; }
            if (show_theme_select) { show_theme_select = false; theme_query = ""; return true; }
            if (state.slash_suggestion_mode) {
                state.slash_suggestion_mode = false;
                state.slash_suggestion_idx = 0;
                return true;
            }
            if (!prompt_input.empty()) {
                prompt_input.clear();
                return true;
            }
            // Nothing to dismiss — fall through
        }
        // ── Tool block keyboard navigation ──
        // ArrowUp/ArrowDown (or k/j) move focus between tool blocks.
        auto navigate_tool = [&](int delta) -> bool {
            if (!state.tool_block_order || state.tool_block_order->empty()) return false;
            int n = static_cast<int>(state.tool_block_order->size());
            int cur = *state.focused_tool_index;
            cur = std::max(-1, std::min(n - 1, cur + delta));
            *state.focused_tool_index = cur;
            return true;
        };
        if (e == Event::ArrowUp || e == Event::Character('k')) {
            if (navigate_tool(-1)) return true;
        }
        if (e == Event::ArrowDown || e == Event::Character('j')) {
            if (navigate_tool(1)) return true;
        }

        // Enter toggles a focused tool block — otherwise let it reach the input
        if (e == Event::Return) {
            if (state.tool_block_order && *state.focused_tool_index >= 0 &&
                *state.focused_tool_index < static_cast<int>(state.tool_block_order->size())) {
                const std::string& id = (*state.tool_block_order)[*state.focused_tool_index];
                if (!state.tool_collapse_state) {
                    state.tool_collapse_state =
                        std::make_shared<std::unordered_map<std::string, bool>>();
                }
                bool cur = (*state.tool_collapse_state)[id];
                (*state.tool_collapse_state)[id] = !cur;
                return true;
            }
            // No tool block focused — fall through so input's CatchEvent can submit
        }
        if (e == Event::Tab) {
            if (state.tab_selected == 1) {
                if (tab_toggle->Focused()) files_menu->TakeFocus();
                else tab_toggle->TakeFocus();
            } else if (state.tab_selected == 0) {
                if (tab_toggle->Focused()) input->TakeFocus();
                else tab_toggle->TakeFocus();
            }
            return true;
        }
        constexpr int kLinesPerWheel = 3;
        constexpr int kLinesPerPage = 20;
        if (e == Event::PageUp) { *state.auto_scroll = false; *state.scroll_line = std::max(0, *state.scroll_line - kLinesPerPage); return true; }
        if (e == Event::PageDown) { *state.scroll_line = std::min(INT_MAX, *state.scroll_line + kLinesPerPage); return true; }
        if (e == Event::End) { *state.auto_scroll = true; *state.scroll_line = INT_MAX; return true; }
        if (e == Event::Home) { *state.auto_scroll = false; *state.scroll_line = 0; return true; }
        if (e.is_mouse()) {
            if (e.mouse().button == Mouse::WheelUp) { *state.auto_scroll = false; *state.scroll_line = std::max(0, *state.scroll_line - kLinesPerWheel); return true; }
            if (e.mouse().button == Mouse::WheelDown) { *state.scroll_line = std::min(INT_MAX, *state.scroll_line + kLinesPerWheel); return true; }
        }
        if (e == Event::Special("\x1b\x31")) { state.tab_selected = 0; return true; }
        if (e == Event::Special("\x1b\x32")) { state.tab_selected = 1; return true; }
        if (e == Event::Special("\x1b\x33")) { state.tab_selected = 2; return true; }
        return false;
    });

    // ═══════════════════════════════════════════════════════════
    //  6. Renderer
    // ═══════════════════════════════════════════════════════════
    // Subscribe to store changes to trigger re-render
    auto render_trigger = store.on_change([&screen]() {
        screen.Post(Event::Custom);
    });

    auto renderer = Renderer(main_container, [&] {
        // Track terminal height for mouse selection calculations
        auto screen_box = ftxui::Terminal::Size();
        state.terminal_height = screen_box.dimy;
        // Drain bus events on the UI thread — this is where store mutations happen
        // (all bus event handlers run synchronously during drain)
        bus->drain();
        
        // Expire old toasts
        store.expire_toasts();
        
        // Status rendered inline in header strip
        
        // ── Filter models dynamically for display ──
        std::vector<ai::tui::ModelEntry> filtered_model_entries;
        for (const auto& e : model_entries) {
            if (matches_query(e.model_name, model_query) ||
                matches_query(e.model_id, model_query) ||
                matches_query(e.category, model_query)) {
                filtered_model_entries.push_back(e);
            }
        }

        // ── Filter sessions dynamically for display ──
        std::vector<ai::tui::db::SessionInfo> filtered_session_entries;
        for (const auto& e : session_entries) {
            if (matches_query(e.title, session_query) ||
                matches_query(e.id, session_query) ||
                matches_query(e.workspace, session_query)) {
                filtered_session_entries.push_back(e);
            }
        }

        // ── Filter themes dynamically for display ──
        std::vector<ai::tui::ThemeEntry> filtered_theme_entries;
        for (const auto& e : theme_entries) {
            if (matches_query(e.name, theme_query) ||
                matches_query(e.description, theme_query)) {
                filtered_theme_entries.push_back(e);
            }
        }

        auto main_view = ai::tui::render_view(
            state, providers_list, selected_provider, selected_model,
            enable_tools, prompt_input,
            show_slash, slash_idx, slash_commands,
            show_model_select, model_select_idx, filtered_model_entries, model_query,
            show_session_select, session_select_idx, filtered_session_entries, session_query,
            show_theme_select, theme_select_idx, filtered_theme_entries, theme_query,
            tab_toggle, files_menu, state.scroll_line, input);
        
        // Everything is in the header strip now — no separate footer
        auto layout = main_view;
        
        // Overlay toasts if any
        auto toasts = store.toasts();
        if (!toasts.empty()) {
            return dbox({
                layout,
                ai::tui::render_toast_overlay(toasts, *state.theme) | vcenter,
            });
        }
        
        return layout;
    });

    screen.Loop(renderer);

    // ═══════════════════════════════════════════════════════════
    //  7. Cleanup
    // ═══════════════════════════════════════════════════════════
    LOG_DEBUG("Main: app exiting, signalling threads...");
    *app_running = false;
    for (auto& t : *background_threads) {
        if (t.joinable()) t.join();
    }
    LOG_DEBUG("Main: exit complete");
}
