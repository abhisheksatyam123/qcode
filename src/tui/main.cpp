#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <thread>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <cstdlib>
#include <cstdio>
#include <array>
#include <filesystem>
#include <algorithm>

#include <ai/openai.h>
#include <ai/anthropic.h>
#include <ai/tools.h>

using namespace ftxui;

struct TodoTask {
    std::string text;
    bool completed = false;
};

std::string find_todo_file() {
    std::vector<std::string> paths = {
        "./todo.md",
        "../todo.md",
        "/home/abhi/notes/scratchpad/task/qcode/active/todo-port-tui/todo.md",
    };
    for (const auto& path : paths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return "";
}

std::vector<TodoTask> parse_todo_tasks() {
    std::vector<TodoTask> tasks;
    std::string path = find_todo_file();
    if (path.empty()) return tasks;

    std::ifstream file_stream(path);
    if (!file_stream.is_open()) return tasks;

    std::string line;
    bool in_tasks_section = false;
    while (std::getline(file_stream, line)) {
        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
        trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

        if (trimmed == "## Tasks") {
            in_tasks_section = true;
            continue;
        }
        if (in_tasks_section && trimmed.rfind("## ", 0) == 0) {
            break;
        }

        if (in_tasks_section) {
            if (trimmed.rfind("- [ ]", 0) == 0 || trimmed.rfind("* [ ]", 0) == 0) {
                std::string content = trimmed.substr(5);
                content.erase(0, content.find_first_not_of(" \t"));
                tasks.push_back({content, false});
            } else if (trimmed.rfind("- [x]", 0) == 0 || trimmed.rfind("* [x]", 0) == 0) {
                std::string content = trimmed.substr(5);
                content.erase(0, content.find_first_not_of(" \t"));
                tasks.push_back({content, true});
            }
        }
    }
    return tasks;
}

std::string get_file_preview(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        return "Error: File does not exist: " + path;
    }
    std::ifstream file(path);
    if (!file.is_open()) {
        return "Error: Could not open file: " + path;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Helper thread-safe-ish state
struct ChatState {
    std::shared_ptr<bool> is_generating = std::make_shared<bool>(false);
    std::shared_ptr<std::vector<std::pair<std::string, std::string>>> chat_history = 
        std::make_shared<std::vector<std::pair<std::string, std::string>>>();
    std::shared_ptr<ai::Messages> messages_history = std::make_shared<ai::Messages>();
    std::shared_ptr<int> total_prompt_tokens = std::make_shared<int>(0);
    std::shared_ptr<int> total_completion_tokens = std::make_shared<int>(0);
    std::shared_ptr<int> total_tokens = std::make_shared<int>(0);
    std::shared_ptr<std::vector<std::string>> modified_files = std::make_shared<std::vector<std::string>>();
};

// Thread worker function
void run_llm_generation(
    std::string provider,
    std::string model,
    std::string system_prompt,
    ai::Messages messages,
    bool enable_tools,
    ScreenInteractive* screen,
    std::shared_ptr<bool> is_generating_flag,
    std::shared_ptr<std::vector<std::pair<std::string, std::string>>> chat_history_ptr,
    std::shared_ptr<ai::Messages> messages_history_ptr,
    std::shared_ptr<int> total_prompt_tokens_ptr,
    std::shared_ptr<int> total_completion_tokens_ptr,
    std::shared_ptr<int> total_tokens_ptr,
    std::shared_ptr<std::vector<std::string>> modified_files_ptr
) {
    try {
        ai::Client client;
        if (provider == "OpenAI") {
            char* api_key_env = std::getenv("OPENAI_API_KEY");
            if (!api_key_env || std::string(api_key_env).empty()) {
                screen->Post([=]() {
                    chat_history_ptr->back().second = "Error: OPENAI_API_KEY environment variable not set.";
                });
                screen->Post([=]() { *is_generating_flag = false; });
                screen->Post(Event::Custom);
                return;
            }
            client = ai::openai::create_client(api_key_env);
        } else if (provider == "Anthropic") {
            char* api_key_env = std::getenv("ANTHROPIC_API_KEY");
            if (!api_key_env || std::string(api_key_env).empty()) {
                screen->Post([=]() {
                    chat_history_ptr->back().second = "Error: ANTHROPIC_API_KEY environment variable not set.";
                });
                screen->Post([=]() { *is_generating_flag = false; });
                screen->Post(Event::Custom);
                return;
            }
            client = ai::anthropic::create_client(api_key_env);
        } else {
            screen->Post([=]() {
                chat_history_ptr->back().second = "Error: Unknown provider: " + provider;
            });
            screen->Post([=]() { *is_generating_flag = false; });
            screen->Post(Event::Custom);
            return;
        }

        if (enable_tools) {
            ai::ToolSet tools;
            tools["run_bash_command"] = ai::create_simple_tool(
                "run_bash_command",
                "Run a bash shell command on the local machine and get the stdout/stderr output.",
                {{"command", "string"}},
                [](const ai::JsonValue& args, const ai::ToolExecutionContext& context) -> ai::JsonValue {
                    std::string command = args["command"].get<std::string>();
                    std::string output;
                    FILE* pipe = popen(command.c_str(), "r");
                    if (!pipe) {
                        return ai::JsonValue{{"error", "Failed to start command execution"}};
                    }
                    std::array<char, 128> buffer;
                    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
                        output += buffer.data();
                    }
                    pclose(pipe);
                    return ai::JsonValue{{"output", output}};
                }
            );

            tools["read_file"] = ai::create_simple_tool(
                "read_file",
                "Read the entire contents of a file at the specified path.",
                {{"path", "string"}},
                [](const ai::JsonValue& args, const ai::ToolExecutionContext& context) -> ai::JsonValue {
                    std::string path = args["path"].get<std::string>();
                    std::ifstream in_f(path);
                    if (!in_f.is_open()) {
                        return ai::JsonValue{{"error", "Failed to open file: " + path}};
                    }
                    std::stringstream buffer;
                    buffer << in_f.rdbuf();
                    return ai::JsonValue{{"content", buffer.str()}};
                }
            );

            tools["write_file"] = ai::create_simple_tool(
                "write_file",
                "Write the specified content to a file at the given path (overwrites existing files).",
                {{"path", "string"}, {"content", "string"}},
                [=](const ai::JsonValue& args, const ai::ToolExecutionContext& context) -> ai::JsonValue {
                    std::string path = args["path"].get<std::string>();
                    std::string content = args["content"].get<std::string>();
                    std::ofstream out_f(path);
                    if (!out_f.is_open()) {
                        return ai::JsonValue{{"error", "Failed to open file for writing: " + path}};
                    }
                    out_f << content;
                    
                    // Add filename to modified files on main thread
                    screen->Post([=]() {
                        if (std::find(modified_files_ptr->begin(), modified_files_ptr->end(), path) == modified_files_ptr->end()) {
                            modified_files_ptr->push_back(path);
                        }
                    });
                    
                    return ai::JsonValue{{"status", "success"}};
                }
            );

            ai::GenerateOptions options;
            options.model = model;
            options.system = system_prompt;
            options.messages = messages;
            options.tools = tools;
            options.max_steps = 10;

            options.on_tool_call_start = [=](const ai::ToolCall& call) {
                screen->Post([=]() {
                    chat_history_ptr->push_back({"System", "🔧 Tool Call [" + call.tool_name + "]: " + call.arguments.dump()});
                });
                screen->Post(Event::Custom);
            };

            options.on_tool_call_finish = [=](const ai::ToolResult& res) {
                screen->Post([=]() {
                    if (res.is_success()) {
                        std::string res_str = res.result.dump();
                        if (res_str.length() > 500) {
                            res_str = res_str.substr(0, 500) + "... (truncated)";
                        }
                        chat_history_ptr->push_back({"System", "✅ Tool Result [" + res.tool_name + "]: " + res_str});
                    } else {
                        chat_history_ptr->push_back({"System", "❌ Tool Failed [" + res.tool_name + "]: " + res.error_message()});
                    }
                });
                screen->Post(Event::Custom);
            };

            auto result = client.generate_text(options);

            screen->Post([=]() {
                *is_generating_flag = false;
                if (result.is_success()) {
                    chat_history_ptr->push_back({"Assistant", result.text});
                    messages_history_ptr->push_back(ai::Message::assistant(result.text));
                    
                    // Update stats
                    *total_prompt_tokens_ptr += result.usage.prompt_tokens;
                    *total_completion_tokens_ptr += result.usage.completion_tokens;
                    *total_tokens_ptr += result.usage.total_tokens;
                } else {
                    chat_history_ptr->push_back({"System", "Error: " + result.error_message()});
                }
            });
            screen->Post(Event::Custom);

        } else {
            ai::GenerateOptions gen_options;
            gen_options.model = model;
            gen_options.system = system_prompt;
            gen_options.messages = messages;

            ai::StreamOptions stream_options(std::move(gen_options));
            auto stream = client.stream_text(stream_options);

            if (stream.has_error()) {
                std::string err = stream.error_message();
                screen->Post([=]() {
                    chat_history_ptr->back().second = "Error: " + err;
                    *is_generating_flag = false;
                });
                screen->Post(Event::Custom);
                return;
            }

            for (const auto& event : stream) {
                if (event.is_text_delta()) {
                    std::string delta = event.text_delta;
                    screen->Post([=]() {
                        chat_history_ptr->back().second += delta;
                    });
                    screen->Post(Event::Custom);
                } else if (event.is_error()) {
                    std::string err = event.error.value_or("Unknown streaming error");
                    screen->Post([=]() {
                        chat_history_ptr->back().second += "\n[Error: " + err + "]";
                    });
                    screen->Post(Event::Custom);
                    break;
                } else if (event.is_finish()) {
                    if (event.usage.has_value()) {
                        ai::Usage usage = event.usage.value();
                        screen->Post([=]() {
                            *total_prompt_tokens_ptr += usage.prompt_tokens;
                            *total_completion_tokens_ptr += usage.completion_tokens;
                            *total_tokens_ptr += usage.total_tokens;
                        });
                    }
                    break;
                }
            }

            screen->Post([=]() {
                *is_generating_flag = false;
                std::string final_response = chat_history_ptr->back().second;
                messages_history_ptr->push_back(ai::Message::assistant(final_response));
            });
            screen->Post(Event::Custom);
        }

    } catch (const std::exception& e) {
        std::string err_msg = e.what();
        screen->Post([=]() {
            chat_history_ptr->back().second = "Exception: " + err_msg;
            *is_generating_flag = false;
        });
        screen->Post(Event::Custom);
    }
}

Element render_logo() {
    return vbox({
        hbox({ text("        ") | color(Color::RGB(22, 184, 243)), text("                         ") | color(Color::RGB(72, 124, 255)) }),
        hbox({ text("█▀▀█    ") | color(Color::RGB(22, 184, 243)), text("  █▀▀▀ █▀▀█ █▀▀▄ █▀▀ ") | color(Color::RGB(72, 124, 255)) | bold }),
        hbox({ text("█  █ ▀▀ ") | color(Color::RGB(22, 184, 243)), text("  █    █  █ █  █ █▀▀ ") | color(Color::RGB(72, 124, 255)) | bold }),
        hbox({ text("▀▀▀█▀   ") | color(Color::RGB(22, 184, 243)), text("  ▀▀▀▀ ▀▀▀▀ ▀▀▀  ▀▀▀ ") | color(Color::RGB(72, 124, 255)) | bold })
    }) | hcenter;
}

Element render_todo_panel() {
    auto tasks = parse_todo_tasks();
    if (tasks.empty()) {
        return vbox({});
    }
    Elements task_elements;
    task_elements.push_back(text(" Todos") | bold | color(Color::RGB(0xEC, 0x5B, 0x2B)));
    task_elements.push_back(separatorLight() | color(Color::RGB(0xEC, 0x5B, 0x2B)));
    for (const auto& task : tasks) {
        if (task.completed) {
            task_elements.push_back(hbox({
                text(" ✓ ") | color(Color::Green),
                text(task.text) | dim
            }));
        } else {
            task_elements.push_back(hbox({
                text(" ○ ") | color(Color::Yellow),
                text(task.text)
            }));
        }
    }
    return vbox(std::move(task_elements)) 
        | border 
        | color(Color::RGB(0xEC, 0x5B, 0x2B))
        | size(WIDTH, EQUAL, 80)
        | hcenter;
}

Element render_settings_panel(Component provider_radio, Component model_radio, Component tools_checkbox, Component system_input) {
    return vbox({
        text(" Settings") | bold | color(Color::RGB(0xEC, 0x5B, 0x2B)),
        separatorLight() | color(Color::RGB(0xEC, 0x5B, 0x2B)),
        hbox({
            vbox({
                text("PROVIDER:") | dim,
                provider_radio->Render()
            }) | flex,
            separatorLight() | color(Color::RGB(0xEC, 0x5B, 0x2B)),
            vbox({
                text("MODEL:") | dim,
                model_radio->Render()
            }) | flex,
        }),
        separatorLight() | color(Color::RGB(0xEC, 0x5B, 0x2B)),
        tools_checkbox->Render(),
        separatorLight() | color(Color::RGB(0xEC, 0x5B, 0x2B)),
        text("SYSTEM PROMPT:") | dim,
        system_input->Render() | border | color(Color::RGB(0xEC, 0x5B, 0x2B))
    }) | border | color(Color::RGB(0xEC, 0x5B, 0x2B));
}

int main() {
    auto screen = ScreenInteractive::Fullscreen();

    // State
    ChatState state;
    std::string prompt_input = "";
    std::string placeholder = "Type your prompt here... (Press Enter to send)";
    std::string system_prompt = "You are a helpful and concise C++ expert assistant.";
    std::string system_prompt_placeholder = "Enter system prompt here...";
    bool enable_tools = false;
    std::string workspace_dir = std::filesystem::current_path().string();

    std::vector<std::string> providers = {"OpenAI", "Anthropic"};
    int selected_provider = 0;

    std::vector<std::string> openai_models = {"gpt-5.4", "gpt-5.4-pro", "gpt-5.4-mini", "gpt-4.1"};
    std::vector<std::string> anthropic_models = {"claude-sonnet-4-6", "claude-opus-4-7", "claude-haiku-4-5"};

    std::vector<std::string> current_models = openai_models;
    int selected_model = 0;

    // Components
    Component provider_radio = Radiobox(&providers, &selected_provider);
    Component model_radio = Radiobox(&current_models, &selected_model);
    Component tools_checkbox = Checkbox("Enable Agent Tools", &enable_tools);
    Component system_input = Input(&system_prompt, &system_prompt_placeholder);

    // Thread pointer
    std::unique_ptr<std::thread> worker_thread;

    auto trigger_generation = [&]() {
        if (*state.is_generating) {
            return;
        }
        if (prompt_input.empty()) {
            return;
        }

        std::string prompt = prompt_input;
        prompt_input = ""; // Clear input immediately

        *state.is_generating = true;
        state.chat_history->push_back({"User", prompt});
        
        if (!enable_tools) {
            state.chat_history->push_back({"Assistant", ""}); // Empty slot for streaming response
        } else {
            state.chat_history->push_back({"System", "Starting agent execution loop..."});
        }

        state.messages_history->push_back(ai::Message::user(prompt));

        std::string provider = providers[selected_provider];
        std::string model = current_models[selected_model];

        if (worker_thread && worker_thread->joinable()) {
            worker_thread->join();
        }
        worker_thread = std::make_unique<std::thread>(
            run_llm_generation,
            provider,
            model,
            system_prompt,
            *state.messages_history,
            enable_tools,
            &screen,
            state.is_generating,
            state.chat_history,
            state.messages_history,
            state.total_prompt_tokens,
            state.total_completion_tokens,
            state.total_tokens,
            state.modified_files
        );
    };

    // Shared submit handlers for home and session prompt fields
    InputOption prompt_option_home;
    int view_mode_selected = 0; // 0 = Home, 1 = Session
    
    Component home_prompt_input;
    Component session_prompt_input;

    prompt_option_home.on_enter = [&]() {
        if (prompt_input.empty() || *state.is_generating) return;
        view_mode_selected = 1; // Transition to Session view immediately!
        session_prompt_input->TakeFocus(); // Set focus to the session input field
        trigger_generation();
    };

    InputOption prompt_option_session;
    prompt_option_session.on_enter = [&]() {
        trigger_generation();
    };

    home_prompt_input = Input(&prompt_input, &placeholder, prompt_option_home);
    session_prompt_input = Input(&prompt_input, &placeholder, prompt_option_session);

    // Tab horizontal toggles inside Session view
    std::vector<std::string> tab_values = {"Chat", "Files", "Stats"};
    int tab_selected = 0;
    auto tab_toggle = Toggle(&tab_values, &tab_selected);

    // Home View Layout (centered prompt, side-by-side todo and settings panels)
    auto home_view_container = Container::Vertical({
        home_prompt_input,
        provider_radio,
        model_radio,
        tools_checkbox,
        system_input,
    });

    auto home_view_renderer = Renderer(home_view_container, [&] {
        auto logo_element = render_logo();
        auto todo_element = render_todo_panel();
        auto settings_element = render_settings_panel(provider_radio, model_radio, tools_checkbox, system_input);

        Element bottom_section;
        auto tasks = parse_todo_tasks();
        if (tasks.empty()) {
            bottom_section = settings_element | size(WIDTH, EQUAL, 80) | hcenter;
        } else {
            bottom_section = hbox({
                todo_element | flex,
                separator() | color(Color::RGB(0xEC, 0x5B, 0x2B)),
                settings_element | flex
            }) | size(WIDTH, EQUAL, 95) | hcenter;
        }

        return vbox({
            filler(),
            logo_element,
            vbox({
                text("What can I help you build today?") | hcenter | dim,
                home_prompt_input->Render() | border | color(Color::RGB(0xEC, 0x5B, 0x2B))
            }) | size(WIDTH, EQUAL, 80) | hcenter,
            filler(),
            bottom_section,
            filler(),
        }) | flex;
    });

    // Session View Tab 1: Chat
    auto chat_view_container = Container::Vertical({
        session_prompt_input
    });

    auto chat_view_renderer = Renderer(chat_view_container, [&] {
        Elements chat_elements;
        if (state.chat_history->empty()) {
            chat_elements.push_back(text("No messages yet. Send a prompt to start a conversation!") | dim | hcenter);
        } else {
            for (const auto& msg : *state.chat_history) {
                if (msg.first == "User") {
                    chat_elements.push_back(
                        vbox({
                            text("User:") | bold | color(Color::Cyan),
                            paragraph(msg.second) | color(Color::CyanLight),
                            separatorLight() | color(Color::RGB(0xEC, 0x5B, 0x2B))
                        })
                    );
                } else if (msg.first == "Assistant") {
                    chat_elements.push_back(
                        vbox({
                            text("Assistant:") | bold | color(Color::RGB(0xEC, 0x5B, 0x2B)),
                            paragraph(msg.second),
                            separatorLight() | color(Color::RGB(0xEC, 0x5B, 0x2B))
                        })
                    );
                } else if (msg.first == "System") {
                    chat_elements.push_back(
                        vbox({
                            text("[System]:") | bold | color(Color::Yellow),
                            paragraph(msg.second) | color(Color::YellowLight),
                            separatorLight() | color(Color::RGB(0xEC, 0x5B, 0x2B))
                        })
                    );
                }
            }
        }

        return vbox({
            vbox(std::move(chat_elements)) | vscroll_indicator | frame | flex,
            separator() | color(Color::RGB(0xEC, 0x5B, 0x2B)),
            vbox({
                text("Prompt:") | dim,
                session_prompt_input->Render() | border | color(Color::RGB(0xEC, 0x5B, 0x2B))
            })
        }) | flex;
    });

    // Session View Tab 2: Files View (Left modified files menu, right content preview)
    int selected_file = 0;
    Component files_menu = Menu(state.modified_files.get(), &selected_file);
    auto files_view_container = Container::Vertical({
        files_menu
    });

    auto files_view_renderer = Renderer(files_view_container, [&] {
        if (state.modified_files->empty()) {
            return vbox({
                filler(),
                text("No files modified in this session yet.") | dim | hcenter,
                filler()
            }) | flex;
        }

        if (selected_file >= (int)state.modified_files->size()) {
            selected_file = 0;
        }

        std::string selected_path = (*state.modified_files)[selected_file];
        std::string content = get_file_preview(selected_path);

        auto file_list_panel = vbox({
            text(" MODIFIED FILES ") | bold | color(Color::RGB(0xEC, 0x5B, 0x2B)),
            separatorLight() | color(Color::RGB(0xEC, 0x5B, 0x2B)),
            files_menu->Render() | frame
        }) | size(WIDTH, EQUAL, 30);

        auto file_preview_panel = vbox({
            text(" PREVIEW: " + selected_path) | bold | color(Color::RGB(0xEE, 0x79, 0x48)),
            separatorLight() | color(Color::RGB(0xEC, 0x5B, 0x2B)),
            text(""),
            hbox({
                text("  "),
                paragraph(content) | vscroll_indicator | frame | flex,
                text("  ")
            }),
            text("")
        }) | flex;

        return hbox({
            file_list_panel,
            separator() | color(Color::RGB(0xEC, 0x5B, 0x2B)),
            file_preview_panel
        }) | border | color(Color::RGB(0xEC, 0x5B, 0x2B)) | flex;
    });

    // Session View Tab 3: Stats
    auto stats_view_renderer = Renderer([&] {
        int hard_limit = 200000;
        int used = *state.total_tokens;
        double used_pct = (double)used / hard_limit * 100.0;
        if (used_pct > 100.0) used_pct = 100.0;

        int bar_width = 40;
        int filled = (int)(used_pct / 100.0 * bar_width);
        if (filled < 0) filled = 0;
        if (filled > bar_width) filled = bar_width;

        std::string filled_bar = "";
        for (int i = 0; i < filled; ++i) filled_bar += "█";
        std::string empty_bar = "";
        for (int i = 0; i < bar_width - filled; ++i) empty_bar += "░";

        double cost = 0.0;
        std::string provider = providers[selected_provider];
        if (provider == "OpenAI") {
            cost = (*state.total_prompt_tokens * 2.50 + *state.total_completion_tokens * 10.00) / 1000000.0;
        } else {
            cost = (*state.total_prompt_tokens * 3.00 + *state.total_completion_tokens * 15.00) / 1000000.0;
        }

        return vbox({
            text(""),
            hbox({
                text("  "),
                vbox({
                    text("⎔ CONTEXT WINDOW") | bold | color(Color::RGB(0xEE, 0x79, 0x48)),
                    hbox({
                        text("Model/Provider: ") | dim,
                        text(providers[selected_provider] + " / " + current_models[selected_model]) | bold
                    }),
                    hbox({
                        text("Usage: ") | dim,
                        text(std::to_string(used) + " / " + std::to_string(hard_limit) + " tokens (" + std::to_string((int)used_pct) + "%)")
                    }),
                    hbox({
                        text("[") | dim,
                        text(filled_bar) | color(used_pct >= 90 ? Color::Red : (used_pct >= 70 ? Color::Yellow : Color::RGB(0xEE, 0x79, 0x48))),
                        text(empty_bar) | dim,
                        text("]") | dim
                    }),
                    separatorLight() | color(Color::RGB(0xEC, 0x5B, 0x2B)),
                    text("⎔ SESSION STATS") | bold | color(Color::RGB(0xEE, 0x79, 0x48)),
                    hbox({ text("Prompt Tokens: ") | dim, text(std::to_string(*state.total_prompt_tokens)) }),
                    hbox({ text("Completion Tokens: ") | dim, text(std::to_string(*state.total_completion_tokens)) }),
                    hbox({ text("Total Tokens: ") | dim, text(std::to_string(*state.total_tokens)) }),
                    hbox({ text("Estimated Cost: ") | dim, text("$" + std::to_string(cost)) | color(Color::Green) | bold }),
                }) | flex,
                text("  ")
            }),
            text("")
        }) | border | color(Color::RGB(0xEC, 0x5B, 0x2B)) | size(WIDTH, LESS_THAN, 80) | hcenter;
    });

    // Session View Top level
    auto tab_container = Container::Tab({
        chat_view_renderer,
        files_view_renderer,
        stats_view_renderer
    }, &tab_selected);

    auto session_view_container = Container::Vertical({
        tab_toggle,
        tab_container
    });

    auto session_view_renderer = Renderer(session_view_container, [&] {
        auto top_bar = vbox({
            hbox({
                tab_toggle->Render(),
                filler(),
                text("Session: " + providers[selected_provider] + " (" + current_models[selected_model] + ")") 
                    | bold | color(Color::RGB(0xEE, 0x79, 0x48))
            }) | border | color(Color::RGB(0xEC, 0x5B, 0x2B))
        });

        return vbox({
            top_bar,
            tab_container->Render() | flex
        }) | flex;
    });

    // Root Switch (Home <-> Session)
    auto main_view_tab = Container::Tab({
        home_view_renderer,
        session_view_renderer
    }, &view_mode_selected);

    // Root CatchEvent for Ctrl-L to reset to Home
    main_view_tab = CatchEvent(main_view_tab, [&](Event event) {
        if (event.input() == "\x0c") { // Ctrl-L
            if (!*state.is_generating) {
                state.chat_history->clear();
                state.messages_history->clear();
                state.modified_files->clear();
                *state.total_prompt_tokens = 0;
                *state.total_completion_tokens = 0;
                *state.total_tokens = 0;
                selected_file = 0;
                view_mode_selected = 0; // Back to home view
                home_prompt_input->TakeFocus(); // Focus back to home input field
                return true;
            }
        }
        return false;
    });

    auto root_renderer = Renderer(main_view_tab, [&] {
        // Sync models list if provider changed
        static int last_provider = -1;
        if (selected_provider != last_provider) {
            last_provider = selected_provider;
            if (selected_provider == 0) {
                current_models = openai_models;
            } else {
                current_models = anthropic_models;
            }
            selected_model = 0;
        }

        auto body = main_view_tab->Render() | flex;

        // Bottom status bar
        auto status_bar = hbox({
            text(" Workspace: " + workspace_dir + " ") | dim,
            filler(),
            text(" [Ctrl-C]: Quit | [Ctrl-L]: Reset View | [Enter]: Send | [Tab]: Move Focus ")
        }) | bgcolor(Color::RGB(0x28, 0x28, 0x28)) | color(Color::White);

        return vbox({
            body,
            status_bar
        }) | bgcolor(Color::RGB(0x0a, 0x0a, 0x0a));
    });

    screen.Loop(root_renderer);

    // Join worker thread before exiting to prevent crashes
    if (worker_thread && worker_thread->joinable()) {
        worker_thread->join();
    }

    return 0;
}
