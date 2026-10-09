#include <csignal>
#include <fstream>
#include <sstream>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/color.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <optional>
#include <mutex>
#include <unistd.h>
#include <vector>
#include <climits>

#include <prompt_wrap.h>

#include <qcode/core/perf.h>
#include <qcode/core/session_file_logger.h>
#include <qcode/core/jthread.h>
#include <qcode/generation/generation_service.h>
#include <qcode/providers/authenticated_providers.h>
#include <qcode/ui/commands.h>
#include <qcode/transform/provider_transform.h>
#include <qcode/config/config.h>
#include <qcode/session/session_store.h>
#include <qcode/session/session_title.h>
#include <qcode/tools/task_tool.h>
#include <nlohmann/json.hpp>
#include <qcode/config/provider_info.h>
#include <qcode/ui/chat_state.h>
#include <qcode/session/system_prompt.h>
#include <views.h>
#include <scroll_helpers.h>
#include <qcode/ui/app_store.h>
#include <qcode/generation/generation_controller.h>
#include <qcode/core/in_process_bus.h>
#include <qcode/core/event.h>
#include <qcode/session/token_budget.h>
#include <qcode/core/identity.h>
#include <qcode/session/git_workspace.h>
#include "picker_helpers.h"
#include "overlays.h"
#include "git_monitor.h"
#include <atomic>
#include <chrono>
#include <functional>

using namespace ftxui;
using namespace qcode::contract;
using qcode::tui::matches_query;
using qcode::tui::sync_session_title;
using qcode::tui::index_of_session;

static void print_tui_usage(const char* argv0) {
    std::cout
        << "Usage: " << argv0 << " [options]\n"
        << "Options:\n"
        << "  --help, -h        Show this help and exit\n"
        << "\n"
        << "Interactive terminal UI. Requires a TTY on stdin and stdout.\n"
        << "For non-interactive prompts, use qcode-cli.\n";
}

// Directory for per-session TUI logs: $QCODE_LOG_DIR, else /tmp/qcode-logs.
// Same resolution as qcode-server so both processes share one log directory.
static std::string tui_log_dir() {
    if (const char* d = std::getenv("QCODE_LOG_DIR")) return d;
    return "/tmp/qcode-logs";
}

int main(int argc, char* argv[]) {
    // A write to a socket/pipe whose peer already closed (Esc aborting an
    // in-flight HTTP stream or a subagent's tool child) raises SIGPIPE, whose
    // default action kills the whole TUI. Ignore it; the write returns EPIPE.
    std::signal(SIGPIPE, SIG_IGN);
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_tui_usage(argv[0]);
            return 0;
        }
        std::cerr << "Unknown option: " << arg << "\n\n";
        print_tui_usage(argv[0]);
        return 2;
    }
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        std::cerr << "qcode-tui requires an interactive terminal (TTY).\n"
                  << "Use --help for usage, or qcode-cli for non-interactive prompts.\n";
        return 1;
    }

    // One log file per session: <dir>/qcode-tui-<session_id>.log. Lines from
    // threads with no bound session (the UI thread) land in <dir>/qcode-tui.log.
    // Each session file has its own rotation budget, so no manual size-based
    // rotation is needed here.
    // Debug mode serializes complete requests and every stream delta. Keeping it
    // enabled in normal runs produced hundreds of megabytes of logs in minutes.
    // QCODE_PERF=1 raises the log to DEBUG so the "[perf]" timing lines appear.
    const std::string log_dir = tui_log_dir();
    auto session_logger = qcode::install_session_file_logger(
        log_dir, "qcode-tui",
        std::getenv("QCODE_PERF") ? qcode::logger::LogLevel::kLogLevelDebug
                                  : qcode::logger::LogLevel::kLogLevelInfo);
    qcode::logger::set_thread_name("main");
    LOG_INFO("q-code starting (bus architecture)...");
    LOG_INFO("logs in {}: qcode-tui-<session>.log", log_dir);

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

    // Subagents tab list. list_tasks reads SQLite (child sessions and their
    // last rows), so it runs on a worker; the next frame applies the result
    // if it is still for the current session. One refresh at a time.
    struct SubagentRefresh {
        std::mutex mu;
        std::string session_id;  // whose entries `ready` holds
        std::optional<std::vector<qcode::SubagentEntry>> ready;
        std::atomic<bool> running{false};
    };
    auto subagent_refresh = std::make_shared<SubagentRefresh>();
    auto refresh_subagents = [&state, &screen, subagent_refresh, app_running]() {
        if (subagent_refresh->running.exchange(true)) return;
        const std::string sid = state.session_id ? *state.session_id : "";
        std::thread([sid, subagent_refresh, app_running, &screen] {
            auto subagent_data = qcode::TaskTool::list_tasks(sid);
            std::vector<qcode::SubagentEntry> entries;
            if (subagent_data.contains("metadata") &&
                subagent_data["metadata"].contains("tasks")) {
                for (const auto& t : subagent_data["metadata"]["tasks"]) {
                    qcode::SubagentEntry entry;
                    entry.status = t.value("status", "");
                    entry.model = t.value("model", "");
                    entry.description = t.value("description", "");
                    entry.task_id = t.value("task_id", t.value("sessionId", ""));
                    entries.push_back(std::move(entry));
                }
            }
            {
                std::lock_guard<std::mutex> lock(subagent_refresh->mu);
                subagent_refresh->session_id = sid;
                subagent_refresh->ready = std::move(entries);
            }
            subagent_refresh->running = false;
            if (app_running->load()) screen.Post(Event::Custom);
        }).detach();
    };
    auto apply_subagent_refresh = [&state, subagent_refresh]() {
        std::lock_guard<std::mutex> lock(subagent_refresh->mu);
        if (!subagent_refresh->ready) return;
        const std::string sid = state.session_id ? *state.session_id : "";
        if (subagent_refresh->session_id == sid) {
            if (!state.subagent_entries) {
                state.subagent_entries =
                    std::make_shared<std::vector<qcode::SubagentEntry>>();
            }
            *state.subagent_entries = std::move(*subagent_refresh->ready);
        }
        subagent_refresh->ready.reset();
    };

    qcode::GenerationController generation(store, bus, app_running);
    // A background subagent finished: redraw, so an idle session picks up
    // its report (see the queue start below).
    qcode::TaskTool::set_notice_listener([&screen]() { screen.Post(Event::Custom); });
    qcode::tui::TuiGitMonitor git_monitor;

    // ── Spinner: advance frame periodically + queue watchdog ──
    std::thread spinner_thread([&store, &generation, app_running, &screen]() {
        qcode::logger::set_thread_name("spinner");
        try {
            while (app_running->load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                const std::string st = store.status_snapshot();
                if (store.is_generating() || st == "generating" || st == "agent") {
                    store.advance_frame();
                } else if (store.has_queued_prompt() && !generation.is_busy() && st != "error") {
                    // Fail-safe watchdog: wake the UI to process pending queued prompts.
                    screen.Post(Event::Custom);
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("spinner_thread exception: {}", e.what());
        }
    });



    // ═══════════════════════════════════════════════════════════
    //  2. Provider & Config setup
    // ═══════════════════════════════════════════════════════════
    std::string prompt_input;
    // Chat input cursor (byte offset) and rendered-bounds boxes. Bound into
    // the FTXUI Input via InputOption::cursor_position so the wrapped
    // renderer and click/arrow handlers share the exact cursor location.
    int prompt_cursor = 0;
    ftxui::Box prompt_rows_content_box;  // absolute bounds of wrapped rows
    ftxui::Box prompt_rows_vp_box;       // absolute bounds of the viewport
    qcode::ToolConfig tool_cfg{true, true};
    std::string system_prompt = qcode::SystemPrompt::build_default(tool_cfg);
    bool enable_tools = true;

    auto providers_list = qcode::load_providers_from_config();
    int selected_provider = 0;
    int selected_model = 0;
    // Mistyped opencode.json values are ignored (defaults apply): say so once.
    if (const auto warnings = qcode::config_warnings(); !warnings.empty()) {
        std::string message = "opencode.json: " + warnings.front();
        if (warnings.size() > 1) {
            message += " (+" + std::to_string(warnings.size() - 1) + " more in the log)";
        }
        store.add_toast(message, "warning", 10000);
    }

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
        if (auto s = qcode::session::get_session_info(last_session)) {
            loaded_prov_id = s->provider;
            loaded_model_id = s->model;
            if (state.session_title) *state.session_title = s->title;
        }
        if (state.session_title && state.session_title->empty()) {
            *state.session_title =
                qcode::session::get_session_title(store.session_id());
        }
        if (!loaded_prov_id.empty() || !loaded_model_id.empty()) {
            if (auto resolved = qcode::tui::resolve_provider_model_indices(
                    providers_list, loaded_prov_id, loaded_model_id)) {
                selected_provider = resolved->first;
                selected_model = resolved->second;
            }
        }
    }
    qcode::update_modified_files(state);
    refresh_subagents();

    // ── Popups state ──
    qcode::tui::TuiOverlayState overlays;
    overlays.model_entries = qcode::build_model_entries(providers_list);
    overlays.session_entries = qcode::session::list_sessions_full();
    bool sessions_dirty = false;
    overlays.theme_entries = qcode::builtin_theme_entries();
    overlays.slash_commands = qcode::builtin_slash_commands();

    // ── TUI Components ──
    std::vector<std::string> tab_values = {"Chat", "Files", "Stats", "Subagents"};
    Component tab_toggle = Toggle(&tab_values, &state.tab_selected);

    InputOption input_opts = InputOption::Default();
    input_opts.multiline = true;
    input_opts.cursor_position = &prompt_cursor;
    input_opts.transform = [&](InputState s) {
        if (s.is_placeholder) {
            // Empty content: FTXUI's placeholder (single short line).
            return s.element | bgcolor(Color::Default);
        }
        // Wrap long lines into vertical rows. FTXUI's stock render draws one
        // row per '\n' and clips (text() never wraps), so without this the
        // box grew but the text scrolled horizontally.
        const int wrap_w = qcode::tui::prompt_box_inner_width(state);
        Element wrapped = qcode::tui::render_wrapped_input(
            prompt_input, prompt_cursor, wrap_w, s.focused, s.hovered,
            &prompt_rows_content_box, &prompt_rows_vp_box);
        return wrapped | bgcolor(Color::Default);
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
            state.agent_mode ? *state.agent_mode : "orchestrator",
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

    auto sync_tool_config_and_system_prompt = [&]() {
        const auto* mi = current_model_info();
        tool_cfg.enable_image = (mi != nullptr && mi->vision);
        system_prompt = qcode::SystemPrompt::build_default(tool_cfg);
    };

    apply_config_variant_if_unset();
    sync_tool_config_and_system_prompt();

    // /reload: re-read opencode.json in place. A running turn or compaction
    // keeps the provider list it copied at its start; the selection carries
    // over by provider/model id (else the first configured model).
    auto reload_config = [&]() {
        std::string provider_id;
        std::string model_id;
        if (const auto* model = current_model_info()) {
            provider_id = providers_list[selected_provider].id;
            model_id = model->id;
        }
        auto fresh = qcode::load_providers_from_config();
        const auto warnings = qcode::config_warnings();
        size_t models = 0;
        for (const auto& provider : fresh) models += provider.models.size();
        if (models == 0) {
            store.add_toast("Reload: opencode.json has no models" +
                                (warnings.empty() ? std::string() : " (" + warnings.front() + ")") +
                                "; kept the current config",
                            "error", 8000);
            return;
        }
        providers_list = std::move(fresh);
        selected_provider = 0;
        selected_model = 0;
        if (auto resolved = qcode::tui::resolve_provider_model_indices(
                providers_list, provider_id, model_id)) {
            selected_provider = resolved->first;
            selected_model = resolved->second;
        } else {
            for (size_t i = 0; i < providers_list.size(); ++i) {
                if (!providers_list[i].models.empty()) {
                    selected_provider = static_cast<int>(i);
                    break;
                }
            }
        }
        qcode::session::seed_model_capabilities(providers_list);
        overlays.model_entries = qcode::build_model_entries(providers_list);
        sync_tool_config_and_system_prompt();
        clamp_variant_to_current_model();
        std::string message = "Reloaded opencode.json: " + std::to_string(models) + " models";
        if (const auto* model = current_model_info(); model && model->id != model_id) {
            message += " · now " + model->id;
        }
        if (!warnings.empty()) {
            message += " · " + std::to_string(warnings.size()) + " warning" +
                       (warnings.size() == 1 ? "" : "s") + ": " + warnings.front();
        }
        store.add_toast(message, warnings.empty() ? "success" : "warning",
                        warnings.empty() ? 3000 : 8000);
    };

    auto open_variant_picker = [&]() {
        qcode::ModelInfo fallback;
        const auto* model = current_model_info();
        overlays.variant_entries = qcode::build_variant_entries(model ? *model : fallback);
        overlays.variant_query = "";
        overlays.variant_select_idx = 0;
        const std::string cur =
            state.reasoning_mode ? *state.reasoning_mode : std::string{};
        const std::string highlight =
            cur.empty() && model
                ? qcode::ProviderTransform::default_variant(*model)
                : cur;
        for (int i = 0; i < static_cast<int>(overlays.variant_entries.size()); ++i) {
            if (overlays.variant_entries[i].id == highlight) {
                overlays.variant_select_idx = i;
                break;
            }
        }
        overlays.show_variant_select = true;
        state.slash_suggestion_mode = false;
        state.slash_suggestion_idx = 0;
        prompt_input.clear();
    };

    auto apply_variant = [&](const std::string& lvl) {
        if (state.reasoning_mode) *state.reasoning_mode = lvl;
        if (state.session_id && !state.session_id->empty()) {
            qcode::session::set_session_modes(
                *state.session_id,
                state.agent_mode ? *state.agent_mode : "orchestrator", lvl);
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
        return true;
    };

    auto any_overlay = [&]() -> bool {
        return overlays.any(state);
    };

    auto close_overlays = [&]() {
        overlays.close_all(state);
    };

    auto open_chat_session = [&](const std::string& id, const std::string& title,
                                 bool remember_return) {
        if (id.empty()) return;
        if (state.session_id && *state.session_id == id) {
            state.tab_selected = 0;
            return;
        }
        if (remember_return && state.session_id && !state.session_id->empty() &&
            state.return_session_id) {
            *state.return_session_id = *state.session_id;
        }
        store.set_session_id(id);
        std::string use_title = title.empty() ? qcode::session::get_session_title(id)
                                              : title;
        sync_session_title(state, use_title);
        state.messages_history->clear();
        qcode::session::reload_session_history(id, state);
        if (state.retry_available) *state.retry_available = false;

        const bool gen_running =
            (id == generation.running_session_id() && generation.is_busy()) ||
            qcode::TaskTool::is_session_running(id);
        store.set_generating(gen_running);
        store.set_status(gen_running ? "generating" : "idle");
        auto pm = qcode::session::get_session_provider_model(id);
        if (!pm.first.empty() || !pm.second.empty()) {
            if (auto resolved = qcode::tui::resolve_provider_model_indices(
                    providers_list, pm.first, pm.second)) {
                selected_provider = resolved->first;
                selected_model = resolved->second;
            }
        }
        sync_tool_config_and_system_prompt();
        auto modes = qcode::session::get_session_modes(id);
        if (state.agent_mode) {
            *state.agent_mode =
                modes.first == "subagent" ? "subagent" : "orchestrator";
        }

        refresh_subagents();
        state.tab_selected = 0;
        store.add_toast("Opened session: " + (use_title.empty() ? id : use_title),
                        "info", 1500);
    };

    auto return_to_parent_session = [&]() -> bool {
        if (!state.return_session_id || state.return_session_id->empty()) return false;
        const std::string pid = *state.return_session_id;
        state.return_session_id->clear();
        open_chat_session(pid, "", false);
        return true;
    };

    auto open_model_picker = [&]() {
        close_overlays();
        overlays.model_entries = qcode::build_model_entries(providers_list);
        overlays.show_model_select = true;
        overlays.model_select_idx = 0;
        overlays.model_query = "";
        for (int i = 0; i < static_cast<int>(overlays.model_entries.size()); i++) {
            if (overlays.model_entries[i].provider_idx == selected_provider &&
                overlays.model_entries[i].model_idx == selected_model) {
                overlays.model_select_idx = i;
                break;
            }
        }
    };

    auto open_session_picker = [&]() {
        close_overlays();
        overlays.session_entries = qcode::session::list_sessions_full();
        sessions_dirty = false;
        if (overlays.session_entries.empty()) {
            store.append_chat_message("System", "No saved sessions found.");
            return;
        }
        overlays.show_session_select = true;
        overlays.session_query = "";
        overlays.session_select_idx = index_of_session(overlays.session_entries, store.session_id());
    };

    auto open_theme_picker = [&]() {
        close_overlays();
        overlays.show_theme_select = true;
        overlays.theme_select_idx = 0;
        overlays.theme_query = "";
        std::string cur = state.theme ? *state.theme : "opencode";
        for (int i = 0; i < static_cast<int>(overlays.theme_entries.size()); i++) {
            if (overlays.theme_entries[i].name == cur) {
                overlays.theme_select_idx = i;
                break;
            }
        }
    };

    auto submit = [&] {
        if (prompt_input.empty()) return;

        // Extract slash command early so control commands work even while generating
        std::string slash_cmd;
        if (prompt_input[0] == '/') {
            slash_cmd = prompt_input.substr(1);
            slash_cmd.erase(slash_cmd.begin(), std::find_if(slash_cmd.begin(), slash_cmd.end(), [](unsigned char ch) {
                return !std::isspace(ch);
            }));
            slash_cmd.erase(std::find_if(slash_cmd.rbegin(), slash_cmd.rend(), [](unsigned char ch) {
                return !std::isspace(ch);
            }).base(), slash_cmd.end());
            std::transform(slash_cmd.begin(), slash_cmd.end(), slash_cmd.begin(), ::tolower);
        }

        // Immediate control commands that must execute even during active generation:
        if (slash_cmd == "clear-queue" || slash_cmd == "clearqueue" || slash_cmd == "cq") {
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
            return;
        }
        if (slash_cmd == "stop" || slash_cmd == "abort") {
            prompt_input = "";
            if (generation.is_active()) {
                if (state.abort_flag && state.abort_flag->load()) {
                    generation.force_stop_ui();
                } else {
                    generation.request_abort();
                    store.add_toast(
                        "Stopping… (Esc or /stop again to force)", "warning", 3000);
                }
            } else if (store.has_queued_prompt()) {
                const auto n = store.queue_size();
                store.clear_prompt_queue();
                store.add_toast(
                    n == 1 ? "Cleared 1 queued prompt"
                           : ("Cleared " + std::to_string(n) + " queued prompts"),
                    "info", 1500);
            } else {
                store.add_toast("No generation active", "info", 1500);
            }
            return;
        }
        if (slash_cmd == "force-stop" || slash_cmd == "forcestop" || slash_cmd == "kill") {
            prompt_input = "";
            generation.force_stop_ui();
            return;
        }
        if (slash_cmd == "exit" || slash_cmd == "quit" || slash_cmd == "q") {
            prompt_input = "";
            screen.Exit();
            return;
        }

        const auto& turn_status = store.status();
        const bool turn_visible = store.is_generating() ||
                                  turn_status == "generating" ||
                                  turn_status == "agent";
        if (turn_visible) {
            if (store.has_queued_prompt()) {
                store.append_to_last_queued_prompt(prompt_input);
                store.add_toast("Prompt merged into queued message", "info", 1500);
            } else {
                store.enqueue_prompt(prompt_input);
                store.add_toast(
                    generation.is_busy() && !store.is_generating()
                        ? "Queued — waiting for previous turn to finish"
                        : "Queued · #1",
                    "info", 1500);
            }
            LOG_INFO("Main: prompt queued/merged (queue_size={})", store.queue_size());
            prompt_input = "";
            *state.auto_scroll = true;
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

            if (cmd == "thinking") {
                prompt_input = "";
                *state.show_thinking = !*state.show_thinking;
                store.add_toast(*state.show_thinking ? "Thinking: shown"
                                                     : "Thinking: hidden",
                                "info", 2000);
                return;
            }

            if (cmd == "stop" || cmd == "abort") {
                prompt_input = "";
                if (generation.is_active()) {
                    generation.request_abort();
                    store.add_toast(
                        "Stopping… (Esc again to force)", "warning", 3000);
                } else if (store.has_queued_prompt()) {
                    const auto n = store.queue_size();
                    store.clear_prompt_queue();
                    store.add_toast(
                        n == 1 ? "Cleared 1 queued prompt"
                               : ("Cleared " + std::to_string(n) + " queued prompts"),
                        "info", 1500);
                } else {
                    store.add_toast("No generation active", "info", 1500);
                }
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
                return;
            }

            if (cmd == "session" || cmd == "list") {
                prompt_input = "";
                open_session_picker();
                return;
            }

            // /session <id> (or /load <id>) switches exactly like the picker:
            // rebind the store (title, totals, usage stats, queue) and the
            // model. Unknown ids fall through to the usage / not-found text.
            if (cmd.rfind("session ", 0) == 0 || cmd.rfind("load ", 0) == 0) {
                std::string id = cmd.substr(cmd.find(' ') + 1);
                id.erase(0, id.find_first_not_of(" \t"));
                const auto listed = [&id] {
                    for (const auto& entry : qcode::session::list_sessions()) {
                        if (entry.first == id) return true;
                    }
                    return false;
                };
                if (qcode::session::is_valid_session_id(id) && listed()) {
                    prompt_input = "";
                    close_overlays();
                    open_chat_session(id, "", false);
                    return;
                }
            }

            if (cmd == "theme" || cmd == "themes") {
                prompt_input = "";
                open_theme_picker();
                return;
            }

            if (cmd == "model" || cmd == "models") {
                prompt_input = "";
                open_model_picker();
                return;
            }

            if (cmd == "variant" || cmd == "variants") {
                prompt_input = "";
                open_variant_picker();
                return;
            }

            if (cmd == "reload") {
                prompt_input = "";
                reload_config();
                sessions_dirty = true;
                return;
            }

            if (cmd == "help" || cmd == "?") {
                prompt_input = "";
                close_overlays();
                overlays.show_help = true;
                return;
            }

            // /new switches sessions: detach the running turn and the old
            // session's queue first; handle_slash_command only creates the
            // row and sets the id, so rebind the store (queue, title) after.
            const bool new_session = cmd == "new" || cmd.rfind("new ", 0) == 0;
            if (new_session) {
                generation.prepare_session_switch();
                store.clear_prompt_queue();
                if (state.subagent_entries) state.subagent_entries->clear();
            }
            prompt_input = "";
            qcode::handle_slash_command(raw, prompt_input, providers_list,
                                          selected_provider, selected_model,
                                          enable_tools, system_prompt, state,
                                          compaction_thread, *bus);
            if (new_session && state.session_id) {
                const std::string title = state.session_title ? *state.session_title : "";
                store.set_session_id(*state.session_id);
                if (!title.empty()) sync_session_title(state, title);
                persist_session_variant();
            }
            sync_tool_config_and_system_prompt();
            clamp_variant_to_current_model();
            sessions_dirty = true;
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
    input |= CatchEvent([&](Event e) {

        if (overlays.show_help) {
            if (e == Event::Escape || e == Event::Return) {
                overlays.show_help = false;
                return true;
            }
            return true;
        }

        if (qcode::tui::handle_model_select_keys(e, overlays, [&](const qcode::ModelEntry& entry) {
            selected_provider = entry.provider_idx;
            selected_model = entry.model_idx;
            sync_tool_config_and_system_prompt();
            clamp_variant_to_current_model();
            if (state.session_id && !state.session_id->empty()) {
                qcode::session::set_session_provider_model(
                    *state.session_id,
                    providers_list[selected_provider].name,
                    providers_list[selected_provider].models[selected_model].name);
            }
            sessions_dirty = true;
            store.add_toast("Switched model: " + entry.model_name, "info", 1500);
        })) return true;

        if (qcode::tui::handle_session_select_keys(e, overlays,
            [&](const qcode::session::SessionInfo& picked) {
                open_chat_session(picked.id, picked.title, false);

                if (!picked.provider.empty() || !picked.model.empty()) {
                    if (auto resolved = qcode::tui::resolve_provider_model_indices(
                            providers_list, picked.provider, picked.model)) {
                        selected_provider = resolved->first;
                        selected_model = resolved->second;
                    }
                }
                apply_config_variant_if_unset();
                sync_tool_config_and_system_prompt();
                store.add_toast("Switched to: " + picked.title, "info", 1500);
            },
            [&](const std::string& sid) {
                qcode::TaskTool::delete_session_tasks(sid);
                qcode::session::delete_session(sid);
            })) return true;

        if (qcode::tui::handle_theme_select_keys(e, overlays, [&](const std::string& new_theme) {
            if (state.theme) *state.theme = new_theme;
            store.append_chat_message("System", "Theme changed to: " + new_theme);
        })) return true;

        if (qcode::tui::handle_variant_select_keys(e, overlays, [&](const std::string& vid) {
            apply_variant(vid);
        })) return true;

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
            for (const auto& cmd : overlays.slash_commands) {
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
                            open_session_picker();
                        } else if (cmd_name == "model") {
                            open_model_picker();
                        } else if (cmd_name == "retry") {
                            trigger_retry();
                        } else if (cmd_name == "theme") {
                            open_theme_picker();
                        } else if (cmd_name == "variant") {
                            open_variant_picker();
                        } else {
                            // Same path as a typed command, so /new also
                            // rebinds the store (queue, title, stats).
                            prompt_input = "/" + cmd_name;
                            submit();
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

        // ── Visual-row arrow navigation in the wrapped chat input ──
        // FTXUI's Input moves by logical '\n' lines only, which jumps from
        // the very start to the very end (and back) on a single wrapped
        // paragraph. Move by wrapped display row instead, preserving the
        // display column; at the first/last row fall through to FTXUI's
        // default logical-line behavior.
        if (state.tab_selected == 0 &&
            (e == Event::ArrowUp || e == Event::ArrowDown)) {
            const int wrap_w = qcode::tui::prompt_box_inner_width(state);
            const int moved = qcode::tui::move_visual_row(
                prompt_input, wrap_w, prompt_cursor,
                e == Event::ArrowUp ? -1 : 1);
            if (moved >= 0) {
                prompt_cursor = moved;
                return true;
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
        input
    });

    main_container |= CatchEvent([&](Event e) {
        if (qcode::tui::handle_variant_select_keys(e, overlays, [&](const std::string& vid) { apply_variant(vid); })) return true;
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
            return true;
        }
        // ── Global Escape: close popups, clear queue, abort, clear input ──
        if (e == Event::Escape) {
            if (overlays.any(state)) {
                overlays.close_all(state);
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
                return true;
            }

            // Child subagent view → back to parent session
            if (state.return_session_id && !state.return_session_id->empty()) {
                if (return_to_parent_session()) return true;
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
                return true;
            }
            if (!prompt_input.empty()) {
                prompt_input.clear();
                return true;
            }
            if (store.has_queued_prompt()) {
                const auto n = store.queue_size();
                store.clear_prompt_queue();
                store.add_toast(
                    n == 1 ? "Cleared 1 queued prompt"
                           : ("Cleared " + std::to_string(n) + " queued prompts"),
                    "info", 1500);
                return true;
            }
        }


        constexpr int kLinesPerWheel = 3;
        if (e.is_mouse()) {
            // ── Click-to-position in the wrapped chat input ──
            // FTXUI's Input maps clicks with logical lines (and its private
            // cursor box, which our wrapped render does not populate), so a
            // click on wrapped text would move the cursor to the wrong
            // place. Map the click through the wrapped rows ourselves.
            if (state.tab_selected == 0 && !any_overlay() &&
                !prompt_input.empty() &&
                e.mouse().button == Mouse::Left &&
                e.mouse().motion == Mouse::Pressed &&
                prompt_rows_vp_box.Contain(e.mouse().x, e.mouse().y)) {
                const int wrap_w = qcode::tui::prompt_box_inner_width(state);
                const auto rows =
                    qcode::tui::wrap_prompt_rows(prompt_input, wrap_w);
                if (!rows.empty()) {
                    int row = e.mouse().y - prompt_rows_content_box.y_min;
                    row = std::clamp(row, 0, (int)rows.size() - 1);
                    const int col = e.mouse().x - prompt_rows_content_box.x_min;
                    prompt_cursor = qcode::tui::byte_at_visual_col(
                        prompt_input, rows[row], col);
                    input->TakeFocus();
                    return true;
                }
            }
            if (state.tab_selected == 1 && !state.files_detail_open) {
                const auto file_count =
                    state.file_changes ? state.file_changes->size() : 0;
                if (file_count > 0) {
                    if (e.mouse().button == Mouse::WheelUp) {
                        state.selected_file = std::max(0, state.selected_file - 1);
                        return true;
                    }
                    if (e.mouse().button == Mouse::WheelDown) {
                        state.selected_file = std::min(
                            static_cast<int>(file_count) - 1, state.selected_file + 1);
                        return true;
                    }
                }
            }
            if (state.tab_selected == 3) {
                const auto& tasks = state.subagent_entries
                                        ? *state.subagent_entries
                                        : std::vector<qcode::SubagentEntry>{};
                const int child_count = static_cast<int>(tasks.size());
                if (child_count > 0) {
                    if (e.mouse().button == Mouse::WheelUp) {
                        state.selected_session_item =
                            std::max(0, state.selected_session_item - 1);
                        return true;
                    }
                    if (e.mouse().button == Mouse::WheelDown) {
                        state.selected_session_item =
                            std::min(child_count - 1, state.selected_session_item + 1);
                        return true;
                    }
                }
            }
            if (e.mouse().button == Mouse::WheelUp) {
                *state.auto_scroll = false;
                *state.scroll_line = std::max(0, *state.scroll_line - kLinesPerWheel);
                return true;
            }
            if (e.mouse().button == Mouse::WheelDown) {
                *state.scroll_line = *state.scroll_line + kLinesPerWheel;
                return true;
            }
            if (e.mouse().button == Mouse::Left && e.mouse().motion == Mouse::Pressed) {
                if (state.tab_selected == 1 && state.files_detail_open &&
                    state.files_back_box) {
                    if (state.files_back_box->Contain(e.mouse().x,
                                                           e.mouse().y)) {
                        state.files_detail_open = false;
                        *state.scroll_line = 0;
                        *state.auto_scroll = true;
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
                            return true;
                        }
                    }
                }
                if (state.return_session_id && !state.return_session_id->empty() &&
                    state.session_back_box &&
                    state.session_back_box->Contain(e.mouse().x, e.mouse().y)) {
                    if (return_to_parent_session()) return true;
                }
                if (state.tab_selected == 3 && state.subagent_row_boxes && state.subagent_entries) {
                    const auto& tasks = *state.subagent_entries;
                    for (size_t i = 0; i < state.subagent_row_boxes->size() &&
                                       i < tasks.size();
                         ++i) {
                        if ((*state.subagent_row_boxes)[i].Contain(e.mouse().x,
                                                                   e.mouse().y)) {
                            state.selected_session_item = static_cast<int>(i);
                            const std::string sid = tasks[i].task_id;
                            const std::string desc = tasks[i].description.empty() ? sid : tasks[i].description;
                            if (!sid.empty()) {
                                open_chat_session(sid, desc, true);
                                store.add_toast("Opened child session: " + desc,
                                                "info", 1500);
                            }
                            return true;
                        }
                    }
                }
                if (state.tool_task_boxes && state.tool_task_sessions) {
                    for (const auto& [id, box] : *state.tool_task_boxes) {
                        if (box.Contain(e.mouse().x, e.mouse().y)) {
                            auto it = state.tool_task_sessions->find(id);
                            if (it != state.tool_task_sessions->end() &&
                                !it->second.empty()) {
                                open_chat_session(it->second, "", true);
                                return true;
                            }
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
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    });

    // ═══════════════════════════════════════════════════════════
    //  6. Renderer
    // ═══════════════════════════════════════════════════════════
    // Subscribe to store changes to trigger re-render. Notifications raised on
    // the UI thread (bus drain inside the Renderer, event handlers) are already
    // followed by a redraw, so only worker-thread notifications need a Post.
    const std::thread::id ui_thread_id = std::this_thread::get_id();
    auto render_trigger = store.on_change([&screen, ui_thread_id]() {
        if (std::this_thread::get_id() != ui_thread_id) {
            screen.Post(Event::Custom);
        }
    });

    bool was_generating = store.is_generating();
    int previous_tab = state.tab_selected;
    qcode::perf::RollingTimer frame_timer("tui_frame");
    auto renderer = Renderer(main_container, [&] {
        const qcode::perf::RollingTimer::Scope frame_scope(frame_timer);
        // Track terminal height for mouse selection calculations. Use the
        // debounced stable size so transient PTY size churn during long bash
        // tool runs does not flip FTXUI's resize detection / force clears.
        state.terminal_height = qcode::tui::stable_terminal_size().dimy;
        // Drain bus events on the UI thread — this is where store mutations happen
        // (all bus event handlers run synchronously during drain)
        bus->drain();
        git_monitor.apply_pending(state);
        apply_subagent_refresh();
        if (was_generating && !store.is_generating()) {
            git_monitor.request_refresh(screen);
            refresh_subagents();
        }

        // Start queued work only after the prior worker has fully exited.
        // Build the request (provider catalog + system prompt) only when a
        // queued prompt is actually waiting.
        // Background subagent reports that arrived while idle start a turn
        // (a running turn takes them at its next model request).
        if (!store.is_generating() && !generation.is_busy() &&
            qcode::TaskTool::has_notices(store.session_id())) {
            for (const auto& notice : qcode::TaskTool::take_notices(store.session_id())) {
                store.enqueue_prompt(notice);
            }
        }
        if (store.has_queued_prompt()) {
            generation.maybe_start_queued(make_generation_request());
        }
        was_generating = store.is_generating();
        if (state.tab_selected == 1 && previous_tab != 1) {
            git_monitor.request_refresh(screen);
            state.files_detail_open = false;
            *state.scroll_line = 0;
        }
        // Stats reload from the DB whenever the tab is opened (one aggregate
        // query); live turns keep them current while it stays open.
        if (state.tab_selected == 2 && previous_tab != 2) *state.scroll_line = 0;
        if (state.tab_selected == 2 && previous_tab != 2 && state.session_id &&
            !state.session_id->empty()) {
            const auto stats = qcode::session::get_session_stats(*state.session_id);
            if (state.total_prompt_tokens) *state.total_prompt_tokens = stats.prompt_tokens;
            if (state.total_completion_tokens)
                *state.total_completion_tokens = stats.completion_tokens;
            if (state.total_tokens) *state.total_tokens = stats.total_tokens;
            if (state.tool_call_count) *state.tool_call_count = stats.tool_calls;
            if (state.total_tool_time_ms)
                *state.total_tool_time_ms = stats.total_tool_time_ms;
        }
        if (state.tab_selected == 3 && previous_tab != 3) {
            refresh_subagents();
            state.selected_session_item = 0;
            *state.scroll_line = 0;
        }
        previous_tab = state.tab_selected;

        if (state.tab_selected == 3) {
            static auto last_subagents_poll = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (now - last_subagents_poll >= std::chrono::seconds(2)) {
                last_subagents_poll = now;
                refresh_subagents();
            }
        }

        // Expire old toasts
        store.expire_toasts();

        // Status rendered inline in header strip

        // Model/theme/variant lists are only read by their popups, so
        // filter them only while open (avoids copying the lists every frame).
        // The session list is also read by the inline /session autocomplete.
        auto filtered_model_entries = overlays.show_model_select
            ? qcode::tui::filter_models(overlays.model_entries, overlays.model_query)
            : std::vector<qcode::ModelEntry>{};
        // The session list is read only by the session picker and the inline
        // /session autocomplete; refresh it lazily when one of them is visible.
        const bool session_list_visible =
            overlays.show_session_select ||
            (prompt_input.size() >= 9 && prompt_input.compare(0, 9, "/session ") == 0);
        if (session_list_visible && sessions_dirty) {
            overlays.session_entries = qcode::session::list_sessions_full();
            sessions_dirty = false;
        }
        auto filtered_session_entries = session_list_visible
            ? qcode::tui::filter_sessions(overlays.session_entries, overlays.session_query)
            : std::vector<qcode::session::SessionInfo>{};
        auto filtered_theme_entries = overlays.show_theme_select
            ? qcode::tui::filter_themes(overlays.theme_entries, overlays.theme_query)
            : std::vector<qcode::ThemeEntry>{};
        auto filtered_variant_entries = overlays.show_variant_select
            ? qcode::tui::filter_variants(overlays.variant_entries, overlays.variant_query)
            : std::vector<qcode::VariantEntry>{};

        auto main_view = qcode::tui::render_view(
            state, providers_list, selected_provider, selected_model,
            enable_tools, prompt_input,
            overlays.show_slash, overlays.slash_idx, overlays.slash_commands,
            overlays.show_model_select, overlays.model_select_idx, filtered_model_entries, overlays.model_query,
            overlays.show_session_select, overlays.session_select_idx, filtered_session_entries, overlays.session_query,
            overlays.show_theme_select, overlays.theme_select_idx, filtered_theme_entries, overlays.theme_query,
            overlays.show_variant_select, overlays.variant_select_idx, filtered_variant_entries,
            overlays.variant_query,
            overlays.show_help,
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

    try {
        screen.Loop(renderer);
    } catch (const std::exception& e) {
        LOG_ERROR("Main: uncaught exception in screen.Loop: {}", e.what());
        std::cerr << "\n[qcode-tui error] " << e.what() << "\n";
    }

    // ═══════════════════════════════════════════════════════════
    //  7. Cleanup
    // ═══════════════════════════════════════════════════════════
    LOG_DEBUG("Main: app exiting, signalling threads...");
    *app_running = false;
    qcode::TaskTool::set_notice_listener(nullptr);
    generation.shutdown();
    qcode::TaskTool::shutdown_background(std::chrono::seconds(3));
    qcode::session_title::shutdown(std::chrono::seconds(2));
    if (compaction_thread->joinable()) {
        compaction_thread->request_stop();
        compaction_thread->join();
    }
    if (spinner_thread.joinable()) spinner_thread.join();
    LOG_DEBUG("Main: exit complete");
    // Flush and close every per-session log file so buffered lines reach disk.
    session_logger->close_all();
    return 0;
}
