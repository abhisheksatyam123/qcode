#include <ai/tui/chat.h>
#include <ai/tui/config.h>
#include <ai/tui/tools.h>
#include <ai/tui/db.h>

#include <cstdlib>
#include <string>
#include <utility>

#include <ai/types/client.h>
#include <ai/logger.h>
#include <ai/openai.h>

namespace ai {
namespace tui {

void run_llm_generation(
    std::string provider,
    std::string model,
    std::string system_prompt,
    ai::Messages messages,
    bool enable_tools,
    std::vector<ProviderInfo> providers_list,
    ftxui::ScreenInteractive* screen,
    ChatState state
) {
    try {
        ai::Client client;
        std::string api_key = "unused", api_url, provider_id;

        for (const auto& p : providers_list) {
            if (p.name == provider) {
                api_url = p.api_url;
                provider_id = p.id;
                break;
            }
        }

        if (provider_id == "openrouter") {
            char* key = std::getenv("OPENROUTER_API_KEY");
            if (!key) {
                screen->Post(
                    [state, screen]() {
                        state.chat_history->push_back({"System", "Error: OPENROUTER_API_KEY not set."});
                        *state.is_generating = false;
                        ai::tui::db::save_message(*state.session_id, "System", "Error: OPENROUTER_API_KEY not set.");
                        screen->Post(ftxui::Event::Custom);
                    });
                return;
            }
            api_key = key;
            client = ai::openai::create_client(api_key,
                                               "https://openrouter.ai/api/v1");
        } else if (provider_id == "qpilot" || provider_id == "qgenie") {
            char* key = std::getenv("QPILOT_API_KEY");
            if (!key) {
                screen->Post(
                    [state, screen]() {
                        state.chat_history->push_back({"System", "Error: QPILOT_API_KEY not set."});
                        *state.is_generating = false;
                        ai::tui::db::save_message(*state.session_id, "System", "Error: QPILOT_API_KEY not set.");
                        screen->Post(ftxui::Event::Custom);
                    });
                return;
            }
            api_key = key;
            client = ai::openai::create_client(
                api_key, provider_id == "qpilot"
                             ? "https://qpilot-api.qualcomm.com"
                             : "https://qgenie-api.qualcomm.com");
        } else if (provider_id == "antigravity") {
            api_key = get_antigravity_token();
            if (api_key.empty()) {
                screen->Post(
                    [state, screen]() {
                        state.chat_history->push_back({"System", "Error: Antigravity token failed."});
                        *state.is_generating = false;
                        ai::tui::db::save_message(*state.session_id, "System", "Error: Antigravity token failed.");
                        screen->Post(ftxui::Event::Custom);
                    });
                return;
            }
            client = ai::openai::create_client(
                api_key,
                "https://daily-cloudcode-pa.googleapis.com/v1internal");
        } else {
            client = ai::openai::create_client(
                "unused",
                api_url.empty() ? "https://opencode.ai/zen/v1" : api_url);
        }

        ai::logger::log_info("LLM gen: provider={}, model={}, tools={}",
                             provider_id, model, enable_tools);

        if (enable_tools) {
            ai::ToolSet tools = Tools::build_definitions(ToolConfig{true, true});

            ai::GenerateOptions options;
            options.model = model;
            options.system = system_prompt;
            options.messages = messages;
            options.tools = tools;
            options.max_steps = 10;

            // Fix dangling reference captures by capturing call and res by value!
            options.on_tool_call_start = [state, screen](const ai::ToolCall& call) {
                screen->Post(
                    [state, screen, call]() {
                        std::string tool_call_str = Tools::format_tool_call(call.tool_name, call.arguments.dump());
                        state.chat_history->push_back({"System", tool_call_str});
                        ai::tui::db::save_message(*state.session_id, "System", tool_call_str);
                        screen->Post(ftxui::Event::Custom);
                    });
            };

            options.on_tool_call_finish = [state, screen](const ai::ToolResult& res) {
                screen->Post([state, screen, res]() {
                    std::string tool_res_str = Tools::format_tool_result(
                             res.tool_name, res.is_success(),
                             res.is_success() ? res.result.dump()
                                              : res.error_message());
                    state.chat_history->push_back({"System", tool_res_str});
                    ai::tui::db::save_message(*state.session_id, "System", tool_res_str);
                    screen->Post(ftxui::Event::Custom);
                });
            };

            auto gen_result = client.generate_text(options);
            screen->Post(
                [state, gen_result, screen]() {
                    *state.is_generating = false;
                    if (gen_result.is_success()) {
                        state.chat_history->push_back(
                            {"Assistant", gen_result.text});
                        
                        // Save assistant response to SQLite
                        ai::tui::db::save_message(*state.session_id, "Assistant", gen_result.text);

                        // Maintain the complete conversation history including assistant tool calls + tool results
                        if (!gen_result.response_messages.empty()) {
                            state.messages_history->insert(
                                state.messages_history->end(),
                                gen_result.response_messages.begin(),
                                gen_result.response_messages.end()
                            );
                        } else {
                            state.messages_history->push_back(
                                ai::Message::assistant(gen_result.text));
                        }

                        *state.total_prompt_tokens +=
                            gen_result.usage.prompt_tokens;
                        *state.total_completion_tokens +=
                            gen_result.usage.completion_tokens;
                        *state.total_tokens += gen_result.usage.total_tokens;
                    } else {
                        std::string err_str = "Error: " + gen_result.error_message();
                        state.chat_history->push_back({"System", err_str});
                        ai::tui::db::save_message(*state.session_id, "System", err_str);
                    }
                    screen->Post(ftxui::Event::Custom);
                });
        } else {
            // Push Assistant placeholder for streaming safely from within the UI loop
            screen->Post([state, screen]() {
                state.chat_history->push_back({"Assistant", ""});
                screen->Post(ftxui::Event::Custom);
            });

            ai::GenerateOptions gen_options;
            gen_options.model = model;
            gen_options.system = system_prompt;
            gen_options.messages = messages;
            ai::StreamOptions stream_options(std::move(gen_options));
            auto stream = client.stream_text(stream_options);

            if (stream.has_error()) {
                screen->Post(
                    [state, screen, err_msg = stream.error_message()]() {
                        state.chat_history->back().second = "Error: " + err_msg;
                        *state.is_generating = false;
                        ai::tui::db::save_message(*state.session_id, "System", "Error: " + err_msg);
                        screen->Post(ftxui::Event::Custom);
                    });
                return;
            }

            for (const auto& event : stream) {
                if (event.is_text_delta()) {
                    screen->Post(
                        [chat_history_ptr = state.chat_history, screen,
                         text = event.text_delta]() {
                            chat_history_ptr->back().second += text;
                            screen->Post(ftxui::Event::Custom);
                        });
                } else if (event.is_error()) {
                    screen->Post(
                        [state, screen]() {
                            state.chat_history->back().second += "\n[Error]";
                            ai::tui::db::save_message(*state.session_id, "System", "Error during streaming");
                            screen->Post(ftxui::Event::Custom);
                        });
                    break;
                } else if (event.is_finish() && event.usage.has_value()) {
                    auto u = event.usage.value();
                    screen->Post(
                        [total_prompt_tokens_ptr = state.total_prompt_tokens,
                         total_completion_tokens_ptr = state.total_completion_tokens,
                         total_tokens_ptr = state.total_tokens,
                         u]() {
                            *total_prompt_tokens_ptr += u.prompt_tokens;
                            *total_completion_tokens_ptr +=
                                u.completion_tokens;
                            *total_tokens_ptr += u.total_tokens;
                        });
                }
            }
            screen->Post(
                [state, screen]() {
                    *state.is_generating = false;
                    std::string assistant_text = state.chat_history->back().second;
                    state.messages_history->push_back(
                        ai::Message::assistant(assistant_text));
                    
                    // Save assistant streaming response to SQLite
                    ai::tui::db::save_message(*state.session_id, "Assistant", assistant_text);
                    screen->Post(ftxui::Event::Custom);
                });
        }
    } catch (const std::exception& e) {
        std::string err_msg = e.what();
        screen->Post(
            [state, screen, err_msg]() {
                state.chat_history->push_back({"System", "Exception: " + err_msg});
                *state.is_generating = false;
                ai::tui::db::save_message(*state.session_id, "System", "Exception: " + err_msg);
                screen->Post(ftxui::Event::Custom);
            });
    }
}

} // namespace tui
} // namespace ai
