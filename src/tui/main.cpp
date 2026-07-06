#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <thread>

#include <ai/file_logger.h>
#include <ai/logger.h>
#include <ai/tui/chat.h>
#include <ai/tui/commands.h>
#include <ai/tui/config.h>
#include <ai/tui/state.h>
#include <ai/tui/system_prompt.h>
#include <ai/tui/tools.h>
#include <ai/tui/views.h>

using namespace ftxui;

int main() {
    ai::install_file_logger("/tmp/qcode.log", ai::logger::LogLevel::kLogLevelDebug);
    ai::logger::log_info("QCode starting...");

    auto screen = ScreenInteractive::Fullscreen();

    // ── State ──
    ai::tui::ChatState state;
    std::string prompt_input;
    ai::tui::ToolConfig tool_cfg{true, true};
    std::string system_prompt = ai::tui::SystemPrompt::build_default(tool_cfg);
    bool enable_tools = true;

    // ── Providers ──
    auto providers_list = ai::tui::load_providers_from_config();
    int selected_provider = 0;
    int selected_model = 0;

    // ── Model selector popup ──
    bool show_model_select = false;
    int model_select_idx = 0;
    auto model_entries = ai::tui::build_model_entries(providers_list);

    // ── Slash command popup ──
    bool show_slash = false;
    int slash_idx = 0;
    auto slash_commands = ai::tui::builtin_slash_commands();

    // ── Input ──
    Component input = Input(&prompt_input, "Type a message or / for commands...");

    input |= CatchEvent([&](Event e) {
        // ── Model selector popup is active ──
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
            // Typing while model selector is open — jump to next match
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
            return true;  // consume all input while popup is open
        }

        // ── Slash command popup is active ──
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
                    // Opening /model from slash popup → open model selector
                    show_model_select = true;
                    model_select_idx = 0;
                    // Highlight current model in the list
                    for (int i = 0; i < static_cast<int>(model_entries.size()); i++) {
                        if (model_entries[i].provider_idx == selected_provider &&
                            model_entries[i].model_idx == selected_model) {
                            model_select_idx = i;
                            break;
                        }
                    }
                } else {
                    // Other commands: fill input with "/cmd "
                    prompt_input = "/" + cmd.name + " ";
                }
                return true;
            }
            if (e == Event::Escape || e == Event::Backspace) {
                show_slash = false;
                return true;
            }
            // Character jump in slash popup
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

        // ── Open slash popup when typing "/" at start of empty input ──
        if (e.is_character() && e.character() == "/" && prompt_input.empty()) {
            show_slash = true;
            slash_idx = 0;
            return true;
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
        state.chat_history->push_back({"Assistant", ""});
        state.messages_history->push_back(ai::Message::user(p));

        std::thread([&, p] {
            ai::tui::run_llm_generation(
                providers_list[selected_provider].name,
                providers_list[selected_provider].models[selected_model].id,
                system_prompt, *state.messages_history, enable_tools,
                providers_list, &screen, state.is_generating,
                state.chat_history, state.messages_history,
                state.total_prompt_tokens, state.total_completion_tokens,
                state.total_tokens, state.modified_files);
        }).detach();
    };

    input |= CatchEvent([&](Event e) {
        if (e == Event::Return && !show_slash && !show_model_select && !prompt_input.empty()) {
            submit();
            return true;
        }
        return false;
    });

    // ── Layout ──
    auto container = Container::Vertical({input});
    auto renderer = Renderer(container, [&] {
        return ai::tui::render_view(
            state, providers_list, selected_provider, selected_model,
            enable_tools,
            show_slash, slash_idx, slash_commands,
            show_model_select, model_select_idx, model_entries,
            input);
    });

    screen.Loop(renderer);
}
