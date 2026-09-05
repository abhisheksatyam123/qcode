#include <fstream>
#include <sstream>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include <climits>

#include <qcode/core/file_logger.h>
#include <qcode/core/jthread.h>
#include <qcode/generation/generation_service.h>
#include <qcode/providers/authenticated_providers.h>
#include <qcode/ui/commands.h>
#include <qcode/transform/provider_transform.h>
#include <qcode/config/config.h>
#include <qcode/session/session_store.h>
#include <qcode/config/provider_info.h>
#include <qcode/ui/chat_state.h>
#include <qcode/session/system_prompt.h>
#include <views.h>
#include <qcode/ui/app_store.h>
#include <qcode/generation/generation_controller.h>
#include <qcode/core/in_process_bus.h>
#include <qcode/core/event.h>
#include <qcode/session/token_budget.h>
#include <qcode/core/identity.h>
#include <qcode/session/git_workspace.h>
#include "picker_helpers.h"
#include <atomic>
#include <chrono>
#include <functional>

using namespace ftxui;
using namespace qcode::contract;
using qcode::tui::matches_query;
using qcode::tui::sync_session_title;
using qcode::tui::index_of_session;

int main() {
    // Rotate oversized logs so a prior debug flood cannot keep filling the disk.
    {
        namespace fs = std::filesystem;
        const fs::path log_path{"/tmp/qcode.log"};
        std::error_code ec;
        if (fs::exists(log_path, ec) && fs::file_size(log_path, ec) > (32ull << 20)) {
            fs::rename(log_path, "/tmp/qcode.log.old", ec);
        }
    }
    // Debug mode serializes complete requests and every stream delta. Keeping it
    // enabled in normal runs produced hundreds of megabytes of logs in minutes.
    qcode::install_file_logger("/tmp/qcode.log", qcode::logger::LogLevel::kLogLevelInfo);
    qcode::logger::set_thread_name("main");
    LOG_INFO("q-code starting (bus architecture)...");

    auto app_running = std::make_shared<std::atomic<bool>>(true);
    auto compaction_thread = std::make_shared<qcode::compat::jthread>();
    auto screen = ScreenInteractive::Fullscreen();

    // ═══════════════════════════════════════════════════════════
    //  1. Initialize the message bus + store
    // ═══════════════════════════════════════════════════════════
    auto bus = std::make_shared<qcode::bus::BusRuntime>();
    register_all_events(*bus);
    bus->set_wake_callback([&screen]() { screen.Post(Event::Custom); });
    // Populate the provider registry once, before any generation thread starts,
    // so resolve() only ever reads a fully-initialized registry.
    qcode::providers::register_authenticated_providers();
    qcode::AppStore store(*bus);

    // Wire bus events to store mutations
    store.wire();

    // ── Legacy state access ──
    auto& state = store.state();

    qcode::GenerationController generation(store, bus, app_running);

    // ── Spinner: advance frame periodically + queue watchdog ──
    std::thread spinner_thread([&store, &generation, app_running, &screen]() {
        qcode::logger::set_thread_name("spinner");
        while (app_running->load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const auto& st = store.status();
            if (store.is_generating() || st == "generating" || st == "agent") {
                store.advance_frame();
            } else if (store.has_queued_prompt() && !generation.is_busy() && st != "error") {
                // Fail-safe watchdog: wake the UI to process pending queued prompts.
                screen.Post(Event::Custom);
            }
        }
    });



    // ═══════════════════════════════════════════════════════════
    //  2. Provider & Config setup
    // ═══════════════════════════════════════════════════════════
    std::string prompt_input;
    qcode::ToolConfig tool_cfg{true, false};
    std::string system_prompt = qcode::SystemPrompt::build_default(tool_cfg);
    bool enable_tools = true;

    auto providers_list = qcode::load_providers_from_config();
    int selected_provider = 0;
    int selected_model = 0;

    // Surface expired Antigravity credentials early instead of failing mid-turn.
    for (const auto& provider : providers_list) {
        if (provider.id.find("antigravity") == std::string::npos) continue;
        if (qcode::antigravity_token_needs_refresh()) {
            store.add_toast(
                "Antigravity token expired — re-login with the Antigravity CLI "
                "or set ANTIGRAVITY_API_KEY",
                "warning", 8000);
        }
        break;
    }

    qcode::session::init_database();
    qcode::session::seed_model_capabilities(providers_list);
    std::string last_session = qcode::session::get_last_active_session();
    if (last_session.empty() && !providers_list.empty()) {
        std::string prov = providers_list[selected_provider].name;
        std::string mod = providers_list[selected_provider].models[selected_model].name;
        last_session = qcode::session::create_new_session(prov, mod);
    }
    store.set_session_id(last_session);
    if (!store.session_id().empty()) {
        qcode::session::reload_session_history(store.session_id(), state);

        // Restore provider and model from the loaded session so the UI
        // matches what was used when the session was last active.
        std::string loaded_prov_id;
        std::string loaded_model_id;
        for (const auto& s : qcode::session::list_sessions_full()) {
            if (s.id == last_session) {
                loaded_prov_id = s.provider;
                loaded_model_id = s.model;
                if (state.session_title) *state.session_title = s.title;
                break;
            }
        }
        if (state.session_title && state.session_title->empty()) {
            *state.session_title =
                qcode::session::get_session_title(store.session_id());
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
    }
    qcode::update_modified_files(state);

    // ── Popups state ──
    bool show_model_select = false;
    int model_select_idx = 0;
    std::string model_query = "";
    auto model_entries = qcode::build_model_entries(providers_list);

    bool show_session_select = false;
    int session_select_idx = 0;
    std::string session_query = "";
    std::vector<qcode::session::SessionInfo> session_entries =
        qcode::session::list_sessions_full();

    bool show_theme_select = false;
    int theme_select_idx = 0;
    std::string theme_query = "";
    std::vector<qcode::ThemeEntry> theme_entries =
        qcode::builtin_theme_entries();

    bool show_variant_select = false;
    int variant_select_idx = 0;
    std::string variant_query = "";
    std::vector<qcode::VariantEntry> variant_entries;

    bool show_palette = false;
    int palette_select_idx = 0;
    std::string palette_query = "";
    const auto palette_commands = qcode::builtin_palette_commands();
    bool show_help = false;

    bool show_slash = false;
    int slash_idx = 0;
    auto slash_commands = qcode::builtin_slash_commands();

    // ── TUI Components ──
    std::vector<std::string> tab_values = {"Chat", "Files", "Stats"};
    Component tab_toggle = Toggle(&tab_values, &state.tab_selected);

    InputOption input_opts = InputOption::Default();
    input_opts.multiline = true;
    input_opts.transform = [](InputState s) {
        s.element = s.element | bgcolor(Color::Default);
        if (s.focused) {
            s.element = s.element | bgcolor(Color::Default);
        }
        return s.element;
    };
    Component input = Input(&prompt_input, "Ask anything...", input_opts);

    // ═══════════════════════════════════════════════════════════
    //  3. Submit handler (uses bus-aware chat)
    // ═══════════════════════════════════════════════════════════
    auto current_model_info = [&]() -> const qcode::ModelInfo* {
        if (selected_provider < 0 ||
            selected_provider >= static_cast<int>(providers_list.size())) {
            return nullptr;
        }
        const auto& models = providers_list[selected_provider].models;
        if (selected_model < 0 ||
            selected_model >= static_cast<int>(models.size())) {
            return nullptr;
        }
        return &models[selected_model];
    };

    auto persist_session_variant = [&]() {
        if (!state.session_id || state.session_id->empty() ||
            !state.reasoning_mode) {
            return;
        }
        qcode::session::set_session_modes(
            *state.session_id,
            state.agent_mode ? *state.agent_mode : "build",
            *state.reasoning_mode);
    };

    // Empty session column = never chosen. Use the model's JSON default.
    // A persisted value is clamped onto that model's advertised efforts.
    auto apply_config_variant_if_unset = [&]() {
        if (!state.reasoning_mode) return;
        const auto* model = current_model_info();
        if (!model) return;
        std::string saved;
        if (state.session_id && !state.session_id->empty()) {
            saved = qcode::session::get_session_modes(*state.session_id).second;
        }
        if (saved.empty()) {
            *state.reasoning_mode =
                qcode::ProviderTransform::default_variant(*model);
            persist_session_variant();
        } else {
            *state.reasoning_mode =
                qcode::ProviderTransform::clamp_variant(*model, saved);
        }
    };

    auto clamp_variant_to_current_model = [&]() {
        if (!state.reasoning_mode) return;
        const auto* model = current_model_info();
        if (!model) return;
        *state.reasoning_mode = qcode::ProviderTransform::resolve_session_variant(
            *model, *state.reasoning_mode);
        persist_session_variant();
    };

    apply_config_variant_if_unset();

    auto open_variant_picker = [&]() {
        qcode::ModelInfo fallback;
        const auto* model = current_model_info();
        variant_entries = qcode::build_variant_entries(model ? *model : fallback);
        variant_query = "";
        variant_select_idx = 0;
        const std::string cur =
            state.reasoning_mode ? *state.reasoning_mode : std::string{};
        const std::string highlight =
            cur.empty() && model
                ? qcode::ProviderTransform::default_variant(*model)
                : cur;
        for (int i = 0; i < static_cast<int>(variant_entries.size()); ++i) {
            if (variant_entries[i].id == highlight) {
                variant_select_idx = i;
                break;
            }
        }
        show_variant_select = true;
        state.slash_suggestion_mode = false;
        state.slash_suggestion_idx = 0;
        prompt_input.clear();
    };

    auto apply_variant = [&](const std::string& lvl) {
        if (state.reasoning_mode) *state.reasoning_mode = lvl;
        if (state.session_id && !state.session_id->empty()) {
            qcode::session::set_session_modes(
                *state.session_id,
                state.agent_mode ? *state.agent_mode : "build", lvl);
        }
        store.add_toast("Variant: " + lvl, lvl == "off" ? "info" : "success",
                        1500);
    };

    auto make_generation_request = [&]() -> qcode::GenerationRequest {
        return qcode::GenerationRequest{
            .providers = providers_list,
            .provider_idx = selected_provider,
            .model_idx = selected_model,
            .system_prompt = system_prompt,
            .tools_enabled = enable_tools,
        };
    };

    auto get_last_user_prompt = [&]() -> std::string {
        if (state.last_user_prompt && !state.last_user_prompt->empty()) {
            return *state.last_user_prompt;
        }
        if (state.messages_history) {
            for (auto it = state.messages_history->rbegin();
                 it != state.messages_history->rend(); ++it) {
                if (it->role == qcode::kMessageRoleUser && !it->has_tool_results()) {
                    std::string t = it->get_text();
                    if (!t.empty()) return t;
                }
            }
        }
        return "";
    };

    auto trigger_retry = [&]() -> bool {
        if (generation.is_active()) {
            store.add_toast("Cannot retry while generation is active", "warning", 2000);
            return false;
        }
        std::string retry_prompt = get_last_user_prompt();
        if (retry_prompt.empty()) {
            store.add_toast("No prompt to retry", "info", 1500);
            return false;
        }

        // Clean trailing artifacts from failed/interrupted turn:
        // Find the last User message matching retry_prompt and drop any
        // trailing System error messages, partial assistant text, or aborted tool calls.
        if (state.messages_history && !state.messages_history->empty()) {
            auto& hist = *state.messages_history;
            for (int i = static_cast<int>(hist.size()) - 1; i >= 0; --i) {
                if (hist[i].role == qcode::kMessageRoleUser && !hist[i].has_tool_results()) {
                    if (hist[i].get_text() == retry_prompt) {
                        hist.erase(hist.begin() + i + 1, hist.end());
                        break;
                    }
                }
            }
            if (state.session_id && !state.session_id->empty()) {
                qcode::session::overwrite_session_history(*state.session_id, hist);
            }
        }

        if (state.last_user_prompt) *state.last_user_prompt = retry_prompt;
        auto req = make_generation_request();
        // Preserved user prompt in history: do not append a duplicate
        req.append_user_message = false;
        store.clear_retry();
        store.clear_error();
        store.add_toast("Retrying: " + (retry_prompt.size() > 30 ? retry_prompt.substr(0, 27) + "…" : retry_prompt), "info", 1500);
        generation.spawn(retry_prompt, std::move(req));
        screen.Post(Event::Custom);
        return true;
    };

    auto any_overlay = [&]() -> bool {
        return show_model_select || show_session_select || show_theme_select ||
               show_variant_select || show_palette || show_help ||
               state.slash_suggestion_mode;
    };

    auto close_overlays = [&]() {
        show_model_select = false;
        model_query = "";
        show_session_select = false;
        session_query = "";
        show_theme_select = false;
        theme_query = "";
        show_variant_select = false;
        variant_query = "";
        show_palette = false;
        palette_query = "";
        show_help = false;
        state.slash_suggestion_mode = false;
        state.slash_suggestion_idx = 0;
    };

    auto toggle_agent_mode = [&]() {
        *state.agent_mode = (*state.agent_mode == "plan") ? "build" : "plan";
        if (state.session_id && !state.session_id->empty()) {
            qcode::session::set_session_modes(
                *state.session_id, *state.agent_mode,
                state.reasoning_mode ? *state.reasoning_mode : "off");
        }
        store.add_toast(*state.agent_mode == "plan"
                            ? "Plan mode: read-only research, no edits"
                            : "Build mode: full tool access",
                        *state.agent_mode == "plan" ? "warning" : "success",
                        2500);
    };

    auto start_new_session = [&]() {
        generation.prepare_session_switch();
        store.clear_prompt_queue();
        std::string prov = providers_list[selected_provider].name;
        std::string mod = providers_list[selected_provider].models[selected_model].name;
        std::string new_id = qcode::session::create_new_session(prov, mod);
        store.set_session_id(new_id);
        state.messages_history->clear();
        std::string title = "Session - " + mod;
        sync_session_title(state, title);
        if (state.retry_available) *state.retry_available = false;
        if (state.last_user_prompt) state.last_user_prompt->clear();
        prompt_input.clear();
        session_entries = qcode::session::list_sessions_full();
        persist_session_variant();
        store.add_toast("Started new session", "success", 2000);
        screen.Post(Event::Custom);
    };

    auto open_model_picker = [&]() {
        close_overlays();
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
    };

    auto open_session_picker = [&]() {
        close_overlays();
        session_entries = qcode::session::list_sessions_full();
        if (session_entries.empty()) {
            store.append_chat_message("System", "No saved sessions found.");
            return;
        }
        show_session_select = true;
        session_query = "";
        session_select_idx = index_of_session(session_entries, store.session_id());
    };

    auto open_theme_picker = [&]() {
        close_overlays();
        show_theme_select = true;
        theme_select_idx = 0;
        theme_query = "";
        std::string cur = state.theme ? *state.theme : "opencode";
        for (int i = 0; i < static_cast<int>(theme_entries.size()); i++) {
            if (theme_entries[i].name == cur) {
                theme_select_idx = i;
                break;
            }
        }
    };

    auto execute_palette_command = [&](const qcode::PaletteCommand& cmd) {
        show_palette = false;
        palette_query = "";
        if (cmd.id == "session_new") {
            start_new_session();
        } else if (cmd.id == "session_list") {
            open_session_picker();
        } else if (cmd.id == "session_rename") {
            prompt_input = "/rename ";
            input->TakeFocus();
            store.add_toast("Enter a new session name", "info", 2000);
        } else if (cmd.id == "session_compact") {
            std::string unused;
            qcode::handle_slash_command(
                "/compact", unused, providers_list, selected_provider,
                selected_model, enable_tools, system_prompt, state,
                compaction_thread, *bus);
        } else if (cmd.id == "session_clear_queue") {
            if (store.has_queued_prompt()) {
                const auto n = store.queue_size();
                store.clear_prompt_queue();
                store.add_toast(
                    n == 1 ? "Cleared 1 queued prompt"
                           : ("Cleared " + std::to_string(n) + " queued prompts"),
                    "info", 1500);
            } else {
                store.add_toast("No queued prompts to clear", "info", 1500);
            }
        } else if (cmd.id == "session_retry") {
            trigger_retry();
        } else if (cmd.id == "model_select") {
            open_model_picker();
        } else if (cmd.id == "model_variant") {
            open_variant_picker();
        } else if (cmd.id == "agent_mode_toggle") {
            toggle_agent_mode();
        } else if (cmd.id == "thinking_toggle") {
            *state.show_thinking = !*state.show_thinking;
            store.add_toast(*state.show_thinking ? "Thinking: shown"
                                                 : "Thinking: hidden",
                            "info", 2000);
        } else if (cmd.id == "files_open") {
            state.tab_selected = 1;
        } else if (cmd.id == "stats_open") {
            state.tab_selected = 2;
        } else if (cmd.id == "theme_select") {
            open_theme_picker();
        } else if (cmd.id == "copy_mode_toggle") {
            *state.copy_mode = !*state.copy_mode;
            screen.TrackMouse(!*state.copy_mode);
            store.add_toast(*state.copy_mode
                                ? "Copy mode ON - select text with mouse, press F3 to return"
                                : "Copy mode OFF",
                            "info", *state.copy_mode ? 4000 : 1500);
            screen.Post(Event::Custom);
        } else if (cmd.id == "help") {
            show_help = true;
        } else if (cmd.id == "exit") {
            screen.Exit();
        }
    };

    auto submit = [&] {
        if (prompt_input.empty()) return;
        const auto& turn_status = store.status();
        const bool turn_visible = store.is_generating() ||
                                  turn_status == "generating" ||
                                  turn_status == "agent";
        if (turn_visible) {
            store.enqueue_prompt(prompt_input);
            const auto n = store.queue_size();
            store.add_toast(
                generation.is_busy() && !store.is_generating()
                    ? "Queued — waiting for previous turn to finish"
                    : "Queued · #" + std::to_string(n),
                "info", 1500);
            LOG_INFO("Main: prompt queued (queue_size={})", n);
            prompt_input = "";
            *state.auto_scroll = true;
            screen.Post(Event::Custom);
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

            if (cmd == "retry") {
                prompt_input = "";
                trigger_retry();
                return;
            }

            if (cmd == "clear-queue" || cmd == "clearqueue" || cmd == "cq") {
                prompt_input = "";
                if (store.has_queued_prompt()) {
                    const auto n = store.queue_size();
                    store.clear_prompt_queue();
                    store.add_toast(
                        n == 1 ? "Cleared 1 queued prompt"
                               : ("Cleared " + std::to_string(n) + " queued prompts"),
                        "info", 1500);
                } else {
                    store.add_toast("No queued prompts to clear", "info", 1500);
                }
                screen.Post(Event::Custom);
                return;
            }

            if (cmd.rfind("queue", 0) == 0) {
                prompt_input = "";
                // /queue          — list queued prompts
                // /queue rm <n>   — remove the nth queued prompt
                const std::string qargs_str =
                    cmd.size() > 5 ? cmd.substr(5) : "";
                std::istringstream qargs(qargs_str);
                std::string sub;
                qargs >> sub;
                if (sub == "rm" || sub == "remove" || sub == "del") {
                    size_t n = 0;
                    if (qargs >> n && store.remove_queued_prompt(n)) {
                        store.add_toast("Removed queued prompt #" + std::to_string(n),
                                        "info", 1500);
                    } else {
                        store.add_toast("Usage: /queue rm <n>  (1..queue size)",
                                        "warning", 2000);
                    }
                } else if (!sub.empty()) {
                    store.add_toast("Usage: /queue [rm <n>]", "warning", 2000);
                } else if (!store.has_queued_prompt()) {
                    store.add_toast("Prompt queue is empty", "info", 1500);
                } else {
                    const auto snapshot = store.queued_prompts_snapshot();
                    for (size_t qi = 0; qi < snapshot.size(); ++qi) {
                        auto body = snapshot[qi];
                        const auto nl = body.find('\n');
                        if (nl != std::string::npos) body = body.substr(0, nl) + " …";
                        if (body.size() > 60) body = body.substr(0, 60) + "…";
                        store.append_chat_message(
                            "System", "#" + std::to_string(qi + 1) + "/" +
                                          std::to_string(snapshot.size()) + ": " + body);
                    }
                    store.add_toast(
                        std::to_string(snapshot.size()) + " queued prompt(s) · /queue rm <n> to remove",
                        "info", 2500);
                }
                screen.Post(Event::Custom);
                return;
            }

            if (cmd == "session" || cmd == "list") {
                prompt_input = "";
                session_entries = qcode::session::list_sessions_full();
                if (!session_entries.empty()) {
                    show_session_select = true;
                    session_query = "";
                    session_select_idx =
                        index_of_session(session_entries, store.session_id());
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
                std::string cur = state.theme ? *state.theme : "opencode";
                for (int i = 0; i < static_cast<int>(theme_entries.size()); i++) {
                    if (theme_entries[i].name == cur) {
                        theme_select_idx = i;
                        break;
                    }
                }
                return;
            }

            if (cmd == "model" || cmd == "models") {
                prompt_input = "";
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
                return;
            }

            if (cmd == "variant" || cmd == "variants") {
                prompt_input = "";
                open_variant_picker();
                return;
            }

            if (cmd == "help" || cmd == "?") {
                prompt_input = "";
                close_overlays();
                show_help = true;
                return;
            }

            prompt_input = "";
            qcode::handle_slash_command(raw, prompt_input, providers_list,
                                          selected_provider, selected_model,
                                          enable_tools, system_prompt, state,
                                          compaction_thread, *bus);
            return;
        }

        // Normal message — hand off to the generation controller.
        std::string p = prompt_input;
        prompt_input = "";
        generation.spawn(std::move(p), make_generation_request());
    };


    // ═══════════════════════════════════════════════════════════
    //  4. Event handlers (keyboard, mouse, etc.)
    // ═══════════════════════════════════════════════════════════
    auto handle_variant_list_keys = [&](Event e) -> bool {
        if (!show_variant_select) return false;
        std::vector<qcode::VariantEntry> filtered;
        for (const auto& entry : variant_entries) {
            if (matches_query(entry.id, variant_query) ||
                matches_query(entry.title, variant_query) ||
                matches_query(entry.description, variant_query)) {
                filtered.push_back(entry);
            }
        }
        if (!filtered.empty()) {
            variant_select_idx = std::clamp(
                variant_select_idx, 0, static_cast<int>(filtered.size()) - 1);
        } else {
            variant_select_idx = 0;
        }
        const bool down = e == Event::ArrowDown || e == Event::Character('j');
        const bool up = e == Event::ArrowUp || e == Event::Character('k');
        if (down && !filtered.empty()) {
            variant_select_idx = std::min(
                variant_select_idx + 1,
                static_cast<int>(filtered.size()) - 1);
            return true;
        }
        if (up && !filtered.empty()) {
            variant_select_idx = std::max(variant_select_idx - 1, 0);
            return true;
        }
        if (e == Event::Return) {
            if (!filtered.empty() && variant_select_idx >= 0 &&
                variant_select_idx < static_cast<int>(filtered.size())) {
                apply_variant(filtered[variant_select_idx].id);
            }
            show_variant_select = false;
            variant_query = "";
            return true;
        }
        if (e == Event::Escape) {
            show_variant_select = false;
            variant_query = "";
            return true;
        }
        if (e == Event::Backspace || e == Event::Special("\x7f")) {
            if (!variant_query.empty()) {
                variant_query.pop_back();
                variant_select_idx = 0;
            }
            return true;
        }
        if (e.is_character() && e != Event::Character('j') &&
            e != Event::Character('k')) {
            variant_query += e.character();
            variant_select_idx = 0;
            return true;
        }
        return true;
    };

    input |= CatchEvent([&](Event e) {
        // ── Command palette (Ctrl-P) ──
        if (e == Event::Special(std::string(1, '\x10'))) {
            if (show_palette) {
                show_palette = false;
                palette_query = "";
            } else {
                close_overlays();
                show_palette = true;
                palette_query = "";
                palette_select_idx = 0;
                input->TakeFocus();
            }
            return true;
        }

        if (show_help) {
            if (e == Event::Escape || e == Event::Return) {
                show_help = false;
                return true;
            }
            return true;
        }

        if (show_palette) {
            if (e == Event::Escape) {
                show_palette = false;
                palette_query = "";
                return true;
            }

            std::vector<qcode::PaletteCommand> filtered;
            for (const auto& entry : palette_commands) {
                if (matches_query(entry.title, palette_query) ||
                    matches_query(entry.id, palette_query) ||
                    matches_query(entry.description, palette_query) ||
                    matches_query(entry.category, palette_query) ||
                    matches_query(entry.shortcut, palette_query)) {
                    filtered.push_back(entry);
                }
            }
            if (!filtered.empty()) {
                palette_select_idx = std::clamp(
                    palette_select_idx, 0,
                    static_cast<int>(filtered.size()) - 1);
            } else {
                palette_select_idx = 0;
            }

            if (e == Event::ArrowDown) {
                if (!filtered.empty()) {
                    palette_select_idx = std::min(
                        palette_select_idx + 1,
                        static_cast<int>(filtered.size()) - 1);
                }
                return true;
            }
            if (e == Event::ArrowUp) {
                if (!filtered.empty()) {
                    palette_select_idx = std::max(palette_select_idx - 1, 0);
                }
                return true;
            }
            if (e == Event::Return) {
                if (!filtered.empty() && palette_select_idx >= 0 &&
                    palette_select_idx < static_cast<int>(filtered.size())) {
                    execute_palette_command(filtered[palette_select_idx]);
                } else {
                    show_palette = false;
                    palette_query = "";
                }
                return true;
            }
            if (e == Event::Backspace || e == Event::Special("\x7f")) {
                if (!palette_query.empty()) {
                    palette_query.pop_back();
                    palette_select_idx = 0;
                }
                return true;
            }
            if (e.is_character()) {
                palette_query += e.character();
                palette_select_idx = 0;
                return true;
            }
            return true;
        }

        // ── Model select popup ──
        if (show_model_select) {
            if (e == Event::Escape) {
                show_model_select = false;
                model_query = "";
                return true;
            }

            std::vector<qcode::ModelEntry> filtered_model_entries;
            for (const auto& entry : model_entries) {
                if (matches_query(entry.model_name, model_query) ||
                    matches_query(entry.model_id, model_query) ||
                    matches_query(entry.category, model_query)) {
                    filtered_model_entries.push_back(entry);
                }
            }

            if (!filtered_model_entries.empty()) {
                model_select_idx = std::clamp(model_select_idx, 0, static_cast<int>(filtered_model_entries.size()) - 1);
            } else {
                model_select_idx = 0;
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
                if (!filtered_model_entries.empty() && model_select_idx >= 0 && model_select_idx < static_cast<int>(filtered_model_entries.size())) {
                    auto& entry = filtered_model_entries[model_select_idx];
                    selected_provider = entry.provider_idx;
                    selected_model = entry.model_idx;
                    system_prompt = qcode::SystemPrompt::build_default(tool_cfg);
                    clamp_variant_to_current_model();
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
            if (e == Event::Escape) {
                show_session_select = false;
                session_query = "";
                return true;
            }

            std::vector<qcode::session::SessionInfo> filtered_session_entries;
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
                    const auto& picked =
                        filtered_session_entries[session_select_idx];
                    generation.prepare_session_switch();
                    store.set_session_id(picked.id);
                    sync_session_title(state, picked.title);
                    state.messages_history->clear();
                    qcode::session::reload_session_history(picked.id, state);
                    if (state.retry_available) *state.retry_available = false;

                    // Restore provider and model from the loaded session
                    if (!picked.provider.empty() && !picked.model.empty()) {
                        for (int i = 0; i < static_cast<int>(providers_list.size()); ++i) {
                            if (providers_list[i].name == picked.provider || providers_list[i].id == picked.provider) {
                                for (int j = 0; j < static_cast<int>(providers_list[i].models.size()); ++j) {
                                    if (providers_list[i].models[j].name == picked.model || providers_list[i].models[j].id == picked.model) {
                                        selected_provider = i;
                                        selected_model = j;
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                    }
                    apply_config_variant_if_unset();
                    store.add_toast("Switched to: " + picked.title, "info", 1500);
                }
                show_session_select = false;
                session_query = "";
                return true;
            }

            if (e == Event::Special(std::string(1, '\x04'))) { // Ctrl-D
                if (!filtered_session_entries.empty()) {
                    std::string sid = filtered_session_entries[session_select_idx].id;
                    qcode::session::delete_session(sid);
                    session_entries = qcode::session::list_sessions_full();
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
            if (e == Event::Escape) {
                show_theme_select = false;
                theme_query = "";
                return true;
            }

            std::vector<qcode::ThemeEntry> filtered_theme_entries;
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

        // ── Variant select popup ──
        if (handle_variant_list_keys(e)) return true;

        // ── /variant <effort> inline list ──
        if (prompt_input.size() >= 9 && prompt_input.rfind("/variant ", 0) == 0) {
            qcode::ModelInfo fallback;
            const auto* model = current_model_info();
            auto all = qcode::build_variant_entries(model ? *model : fallback);
            const std::string filter = prompt_input.substr(9);
            std::vector<qcode::VariantEntry> matches;
            for (const auto& v : all) {
                if (filter.empty() || matches_query(v.id, filter) ||
                    matches_query(v.title, filter)) {
                    matches.push_back(v);
                }
            }
            if (!matches.empty()) {
                if (!state.slash_suggestion_mode) {
                    state.slash_suggestion_mode = true;
                    state.slash_suggestion_idx = 0;
                }
                state.slash_suggestion_idx = std::clamp(
                    state.slash_suggestion_idx, 0,
                    static_cast<int>(matches.size()) - 1);
                if (e == Event::ArrowDown) {
                    state.slash_suggestion_idx =
                        (state.slash_suggestion_idx + 1) %
                        static_cast<int>(matches.size());
                    return true;
                }
                if (e == Event::ArrowUp) {
                    state.slash_suggestion_idx =
                        (state.slash_suggestion_idx - 1 +
                         static_cast<int>(matches.size())) %
                        static_cast<int>(matches.size());
                    return true;
                }
                if (e == Event::Return || e == Event::Tab) {
                    apply_variant(matches[state.slash_suggestion_idx].id);
                    prompt_input = "";
                    state.slash_suggestion_mode = false;
                    state.slash_suggestion_idx = 0;
                    return true;
                }
            }
        }

        // ── Slash completion popup ──
        if (!prompt_input.empty() && prompt_input[0] == '/') {
            std::string partial = prompt_input.substr(1);
            std::vector<qcode::SlashCommand> command_matches;
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
                            session_entries = qcode::session::list_sessions_full();
                            if (!session_entries.empty()) {
                                show_session_select = true;
                                session_query = "";
                                session_select_idx = index_of_session(
                                    session_entries, store.session_id());
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
                        } else if (cmd_name == "retry") {
                            trigger_retry();
                        } else if (cmd_name == "theme") {
                            show_theme_select = true;
                            theme_select_idx = 0;
                            theme_query = "";
                            std::string cur = state.theme ? *state.theme : "opencode";
                            for (int i = 0; i < static_cast<int>(theme_entries.size()); i++) {
                                if (theme_entries[i].name == cur) {
                                    theme_select_idx = i;
                                    break;
                                }
                            }
                        } else if (cmd_name == "variant") {
                            open_variant_picker();
                        } else {
                            std::string raw = "/" + cmd_name;
                            qcode::handle_slash_command(raw, prompt_input, providers_list,
                                                          selected_provider, selected_model,
                                                          enable_tools, system_prompt, state,
                                                          compaction_thread, *bus);
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
                    if (e == Event::ArrowUp) {
                        if (state.slash_suggestion_idx > 0) state.slash_suggestion_idx--;
                        else state.slash_suggestion_idx = max_idx;
                        return true;
                    }
                    if (e == Event::ArrowDown) {
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

        // One-key retry (r/R): triggers when retry is available and prompt input is empty.
        // Intercepted BEFORE the Input component processes characters so 'r'/'R' is not typed into input.
        if (state.tab_selected == 0 && prompt_input.empty() && !any_overlay() &&
            !generation.is_active() &&
            (state.retry_available && *state.retry_available) &&
            (e == Event::Character('r') || e == Event::Character('R'))) {
            if (trigger_retry()) {
                return true;
            }
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
        input
    });

    main_container |= CatchEvent([&](Event e) {
        if (handle_variant_list_keys(e)) return true;
        // Toggle visibility of reasoning/thinking blocks (Ctrl-T or F2)
        if (e == Event::Special(std::string(1, '\x14')) || e == Event::F2) {
            *state.show_thinking = !*state.show_thinking;
            store.add_toast(*state.show_thinking ? "Thinking: shown"
                                                 : "Thinking: hidden",
                            "info", 2000);
            return true;
        }
        // Command palette (Ctrl-P)
        if (e == Event::Special(std::string(1, '\x10'))) {
            if (show_palette) {
                show_palette = false;
                palette_query = "";
            } else {
                close_overlays();
                show_palette = true;
                palette_query = "";
                palette_select_idx = 0;
                input->TakeFocus();
            }
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
        // ── New Session shortcut (Ctrl-N) ──
        if (e == Event::Special(std::string(1, '\x0e'))) {
            start_new_session();
            return true;
        }
        // ── Global Escape: close popups, clear queue, abort, clear input ──
        if (e == Event::Escape) {
            if (show_model_select) { show_model_select = false; model_query = ""; return true; }
            if (show_session_select) { show_session_select = false; session_query = ""; return true; }
            if (show_theme_select) { show_theme_select = false; theme_query = ""; return true; }
            if (show_variant_select) {
                show_variant_select = false;
                variant_query = "";
                return true;
            }
            if (show_palette) {
                show_palette = false;
                palette_query = "";
                return true;
            }
            if (show_help) {
                show_help = false;
                return true;
            }
            if (state.slash_suggestion_mode) {
                state.slash_suggestion_mode = false;
                state.slash_suggestion_idx = 0;
                return true;
            }
            // Files tab detail → back to the changed-file list.
            if (state.tab_selected == 1 && state.files_detail_open) {
                state.files_detail_open = false;
                *state.scroll_line = 0;
                screen.Post(Event::Custom);
                return true;
            }

            // Abort takes priority over clearing the prompt while a turn runs
            // (or while a force-stopped worker is still finishing HTTP).
            if (generation.is_active()) {
                if (state.abort_flag && state.abort_flag->load()) {
                    // Second Esc: force-unstick the UI even if the worker is
                    // blocked inside a sync HTTP/tool call. Never join here.
                    generation.force_stop_ui();
                } else {
                    generation.request_abort();
                    store.add_toast(
                        "Stopping… (Esc again to force)", "warning", 3000);
                }
                screen.Post(Event::Custom);
                return true;
            }
            if (!prompt_input.empty()) {
                prompt_input.clear();
                return true;
            }
        }


        // ── Tool block keyboard navigation (only when prompt is empty) ──
        // j/k focus tools, h/← collapse (3 lines), l/→ expand (full), Enter toggles.
        const bool tool_keys_active =
            prompt_input.empty() && !any_overlay();
        auto ensure_tool_focused = [&]() -> bool {
            if (!state.tool_block_order || state.tool_block_order->empty()) {
                return false;
            }
            if (*state.focused_tool_index < 0 ||
                *state.focused_tool_index >=
                    static_cast<int>(state.tool_block_order->size())) {
                *state.focused_tool_index =
                    static_cast<int>(state.tool_block_order->size()) - 1;
            }
            return true;
        };
        auto navigate_tool = [&](int delta) -> bool {
            if (!state.tool_block_order || state.tool_block_order->empty()) {
                return false;
            }
            int n = static_cast<int>(state.tool_block_order->size());
            int cur = *state.focused_tool_index;
            if (cur < 0) {
                cur = delta > 0 ? 0 : n - 1;
            } else {
                cur = std::max(0, std::min(n - 1, cur + delta));
            }
            *state.focused_tool_index = cur;
            screen.Post(Event::Custom);
            return true;
        };
        auto set_focused_tool_collapsed = [&](bool collapsed) -> bool {
            if (!ensure_tool_focused()) return false;
            const std::string& id =
                (*state.tool_block_order)[*state.focused_tool_index];
            if (!state.tool_collapse_state) {
                state.tool_collapse_state =
                    std::make_shared<std::unordered_map<std::string, bool>>();
            }
            (*state.tool_collapse_state)[id] = collapsed;
            screen.Post(Event::Custom);
            return true;
        };
        if (tool_keys_active) {
            if (e == Event::ArrowUp) {
                if (navigate_tool(-1)) return true;
            }
            if (e == Event::ArrowDown) {
                if (navigate_tool(1)) return true;
            }
            if (e == Event::ArrowLeft) {
                if (set_focused_tool_collapsed(true)) return true;
            }
            if (e == Event::ArrowRight) {
                if (set_focused_tool_collapsed(false)) return true;
            }

        }
        if (e == Event::Tab) {
            if (any_overlay()) return true;
            toggle_agent_mode();
            return true;
        }

        // ── Files tab: list navigation / open diff / refresh ──
        if (state.tab_selected == 1 && !any_overlay()) {
            const auto file_count =
                state.file_changes ? state.file_changes->size() : 0;
            if (!state.files_detail_open && file_count > 0) {
                if (e == Event::ArrowUp || e == Event::Character('k')) {
                    state.selected_file =
                        std::max(0, state.selected_file - 1);
                    screen.Post(Event::Custom);
                    return true;
                }
                if (e == Event::ArrowDown || e == Event::Character('j')) {
                    state.selected_file = std::min(
                        state.selected_file + 1,
                        static_cast<int>(file_count) - 1);
                    screen.Post(Event::Custom);
                    return true;
                }
                if (e == Event::Return) {
                    state.files_detail_open = true;
                    *state.scroll_line = 0;
                    *state.auto_scroll = true;
                    screen.Post(Event::Custom);
                    return true;
                }
            }
            if (e == Event::Character('r') || e == Event::Character('R')) {
                qcode::update_modified_files(state);
                state.files_detail_open = false;
                store.add_toast("Refreshed git changes", "info", 1000);
                screen.Post(Event::Custom);
                return true;
            }
        }

        // ── Stats tab: refresh session stats on r ──
        if (state.tab_selected == 2 && !any_overlay()) {
            if (e == Event::Character('r') || e == Event::Character('R')) {
                if (state.session_id && !state.session_id->empty()) {
                    qcode::session::SessionStats stats =
                        qcode::session::get_session_stats(*state.session_id);
                    if (state.total_prompt_tokens)
                        *state.total_prompt_tokens = stats.prompt_tokens;
                    if (state.total_completion_tokens)
                        *state.total_completion_tokens = stats.completion_tokens;
                    if (state.total_tokens)
                        *state.total_tokens = stats.total_tokens;
                    if (state.tool_call_count)
                        *state.tool_call_count = stats.tool_calls;
                    if (state.total_tool_time_ms)
                        *state.total_tool_time_ms = stats.total_tool_time_ms;
                }
                store.add_toast("Refreshed session stats", "info", 1000);
                screen.Post(Event::Custom);
                return true;
            }
        }
        constexpr int kLinesPerWheel = 3;
        constexpr int kLinesPerPage = 20;
        if (e == Event::PageUp) {
            *state.auto_scroll = false;
            if (state.tab_selected == 0) {
                const auto page =
                    static_cast<size_t>(std::max(8, state.terminal_height / 2));
                *state.history_window_start =
                    *state.history_window_start > page
                        ? *state.history_window_start - page
                        : 0;
                *state.scroll_line = 0;
            } else {
                *state.scroll_line =
                    std::max(0, *state.scroll_line - kLinesPerPage);
            }
            return true;
        }
        if (e == Event::PageDown) {
            *state.auto_scroll = false;
            if (state.tab_selected == 0) {
                const auto page =
                    static_cast<size_t>(std::max(8, state.terminal_height / 2));
                *state.history_window_start = std::min(
                    state.messages_history->size(),
                    *state.history_window_start + page);
                *state.scroll_line = INT_MAX;
            } else {
                *state.scroll_line =
                    std::min(INT_MAX, *state.scroll_line + kLinesPerPage);
            }
            return true;
        }
        if (e == Event::End) {
            *state.auto_scroll = true;
            *state.scroll_line = INT_MAX;
            return true;
        }
        if (e == Event::Home) {
            *state.auto_scroll = false;
            if (state.tab_selected == 0) {
                *state.history_window_start = 0;
            }
            *state.scroll_line = 0;
            return true;
        }
        if (e.is_mouse()) {
            if (e.mouse().button == Mouse::WheelUp) { *state.auto_scroll = false; *state.scroll_line = std::max(0, *state.scroll_line - kLinesPerWheel); return true; }
            if (e.mouse().button == Mouse::WheelDown) { *state.scroll_line = std::min(INT_MAX, *state.scroll_line + kLinesPerWheel); return true; }
            if (e.mouse().button == Mouse::Left && e.mouse().motion == Mouse::Pressed) {
                if (state.tab_selected == 1 && state.files_detail_open &&
                    state.files_back_box) {
                    if (state.files_back_box->Contain(e.mouse().x,
                                                           e.mouse().y)) {
                        state.files_detail_open = false;
                        *state.scroll_line = 0;
                        *state.auto_scroll = true;
                        screen.Post(Event::Custom);
                        return true;
                    }
                }
                if (state.tab_selected == 1 && !state.files_detail_open &&
                    state.file_row_boxes) {
                    for (size_t i = 0; i < state.file_row_boxes->size(); ++i) {
                        if ((*state.file_row_boxes)[i].Contain(e.mouse().x,
                                                               e.mouse().y)) {
                            state.selected_file = static_cast<int>(i);
                            state.files_detail_open = true;
                            *state.scroll_line = 0;
                            *state.auto_scroll = true;
                            screen.Post(Event::Custom);
                            return true;
                        }
                    }
                }
                if (state.tool_arrow_boxes) {
                    for (const auto& [id, box] : *state.tool_arrow_boxes) {
                        if (box.Contain(e.mouse().x, e.mouse().y)) {
                            if (!state.tool_collapse_state) {
                                state.tool_collapse_state = std::make_shared<
                                    std::unordered_map<std::string, bool>>();
                            }
                            bool currently_collapsed =
                                !state.tool_collapse_state->count(id) ||
                                (*state.tool_collapse_state)[id];
                            (*state.tool_collapse_state)[id] = !currently_collapsed;
                            screen.Post(Event::Custom);
                            return true;
                        }
                    }
                }
                // Clicking a "+/- Thought" header toggles that thinking trace
                // (opencode parity).
                if (state.thinking_header_boxes && state.thinking_expand_state) {
                    for (const auto& [key, box] : *state.thinking_header_boxes) {
                        if (box.Contain(e.mouse().x, e.mouse().y)) {
                            bool expanded = false;
                            if (auto it = state.thinking_expand_state->find(key);
                                it != state.thinking_expand_state->end()) {
                                expanded = it->second;
                            }
                            (*state.thinking_expand_state)[key] = !expanded;
                            screen.Post(Event::Custom);
                            return true;
                        }
                    }
                }
            }
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

    bool was_generating = store.is_generating();
    int previous_tab = state.tab_selected;
    // Async git refresh — off the UI thread (opencode parity: sdk async).
    // Debounced 500ms and single-flight so rapid generation completions / tab
    // switches don't spawn popen storms. Result is posted back via screen.Post
    // and applied on the UI thread to avoid data races on state.file_changes.
    auto git_refresh_running = std::make_shared<std::atomic<bool>>(false);
    auto git_last_refresh = std::make_shared<std::chrono::steady_clock::time_point>(
        std::chrono::steady_clock::now() - std::chrono::seconds(10));
    auto git_pending_mutex = std::make_shared<std::mutex>();
    auto git_pending = std::make_shared<std::optional<std::vector<qcode::FileChangeEntry>>>();
    auto async_refresh_files = [&, git_refresh_running, git_last_refresh, git_pending, git_pending_mutex]() {
        if (git_refresh_running->exchange(true)) return;
        auto now = std::chrono::steady_clock::now();
        if (now - *git_last_refresh < std::chrono::milliseconds(500)) {
            git_refresh_running->store(false);
            return;
        }
        *git_last_refresh = now;
        std::thread([git_pending, git_pending_mutex, &screen, git_refresh_running]() {
            auto vec = qcode::fetch_modified_files();
            {
                std::lock_guard<std::mutex> lock(*git_pending_mutex);
                *git_pending = std::move(vec);
            }
            git_refresh_running->store(false);
            screen.Post(Event::Custom);
        }).detach();
    };
    auto renderer = Renderer(main_container, [&] {
        // Track terminal height for mouse selection calculations. Use the
        // debounced stable size so transient PTY size churn during long bash
        // tool runs does not flip FTXUI's resize detection / force clears.
        state.terminal_height = qcode::tui::stable_terminal_size().dimy;
        // Drain bus events on the UI thread — this is where store mutations happen
        // (all bus event handlers run synchronously during drain)
        bus->drain();
        // Apply async git results on the UI thread (thread-safe handoff)
        {
            std::lock_guard<std::mutex> lock(*git_pending_mutex);
            if (git_pending->has_value()) {
                if (!state.file_changes) {
                    state.file_changes = std::make_shared<std::vector<qcode::FileChangeEntry>>();
                }
                *state.file_changes = std::move(**git_pending);
                ++*state.files_revision;
                if (state.file_changes->empty()) {
                    state.selected_file = 0;
                    state.files_detail_open = false;
                } else {
                    state.selected_file = std::clamp(state.selected_file, 0,
                        static_cast<int>(state.file_changes->size()) - 1);
                }
                git_pending->reset();
            }
        }
        if (was_generating && !store.is_generating()) {
            async_refresh_files();
        }

        // Start queued work only after the prior worker has fully exited.
        generation.maybe_start_queued(make_generation_request());
        was_generating = store.is_generating();
        if (state.tab_selected == 1 && previous_tab != 1) {
            async_refresh_files();
            state.files_detail_open = false;
            *state.scroll_line = 0;
        }
        previous_tab = state.tab_selected;
        
        // Expire old toasts
        store.expire_toasts();
        
        // Status rendered inline in header strip
        
        // ── Filter models dynamically for display ──
        std::vector<qcode::ModelEntry> filtered_model_entries;
        for (const auto& e : model_entries) {
            if (matches_query(e.model_name, model_query) ||
                matches_query(e.model_id, model_query) ||
                matches_query(e.category, model_query)) {
                filtered_model_entries.push_back(e);
            }
        }

        // ── Filter sessions dynamically for display ──
        std::vector<qcode::session::SessionInfo> filtered_session_entries;
        for (const auto& e : session_entries) {
            if (matches_query(e.title, session_query) ||
                matches_query(e.id, session_query) ||
                matches_query(e.workspace, session_query)) {
                filtered_session_entries.push_back(e);
            }
        }

        // ── Filter themes dynamically for display ──
        std::vector<qcode::ThemeEntry> filtered_theme_entries;
        for (const auto& e : theme_entries) {
            if (matches_query(e.name, theme_query) ||
                matches_query(e.description, theme_query)) {
                filtered_theme_entries.push_back(e);
            }
        }

        std::vector<qcode::VariantEntry> filtered_variant_entries;
        for (const auto& e : variant_entries) {
            if (matches_query(e.id, variant_query) ||
                matches_query(e.title, variant_query) ||
                matches_query(e.description, variant_query)) {
                filtered_variant_entries.push_back(e);
            }
        }

        std::vector<qcode::PaletteCommand> filtered_palette_entries;
        for (const auto& e : palette_commands) {
            if (matches_query(e.title, palette_query) ||
                matches_query(e.id, palette_query) ||
                matches_query(e.description, palette_query) ||
                matches_query(e.category, palette_query) ||
                matches_query(e.shortcut, palette_query)) {
                filtered_palette_entries.push_back(e);
            }
        }

        auto main_view = qcode::tui::render_view(
            state, providers_list, selected_provider, selected_model,
            enable_tools, prompt_input,
            show_slash, slash_idx, slash_commands,
            show_model_select, model_select_idx, filtered_model_entries, model_query,
            show_session_select, session_select_idx, filtered_session_entries, session_query,
            show_theme_select, theme_select_idx, filtered_theme_entries, theme_query,
            show_variant_select, variant_select_idx, filtered_variant_entries,
            variant_query,
            show_palette, palette_select_idx, filtered_palette_entries, palette_query,
            show_help,
            tab_toggle, state.scroll_line, input);

        auto layout = main_view;

        // Toasts sit just above the footer bar.
        auto toasts = store.toasts();
        if (!toasts.empty()) {
            return dbox({
                layout,
                vbox({
                    filler() | flex,
                    qcode::tui::render_toast_overlay(toasts, *state.theme),
                    text("") | size(HEIGHT, EQUAL, 2),
                }),
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
    generation.shutdown();
    if (compaction_thread->joinable()) {
        compaction_thread->request_stop();
        compaction_thread->join();
    }
    if (spinner_thread.joinable()) spinner_thread.join();
    LOG_DEBUG("Main: exit complete");
}
