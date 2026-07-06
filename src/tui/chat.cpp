#include <ai/tui/chat.h>
#include <ai/tui/config.h>
#include <ai/tui/tools.h>

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
    std::shared_ptr<bool> is_generating_flag,
    std::shared_ptr<std::vector<std::pair<std::string, std::string>>> chat_history_ptr,
    std::shared_ptr<ai::Messages> messages_history_ptr,
    std::shared_ptr<int> total_prompt_tokens_ptr,
    std::shared_ptr<int> total_completion_tokens_ptr,
    std::shared_ptr<int> total_tokens_ptr,
    std::shared_ptr<std::vector<std::string>> /*modified_files_ptr*/
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
                    [chat_history_ptr, is_generating_flag, screen]() {
                        chat_history_ptr->back().second =
                            "Error: OPENROUTER_API_KEY not set.";
                        *is_generating_flag = false;
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
                    [chat_history_ptr, is_generating_flag, screen]() {
                        chat_history_ptr->back().second =
                            "Error: QPILOT_API_KEY not set.";
                        *is_generating_flag = false;
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
                    [chat_history_ptr, is_generating_flag, screen]() {
                        chat_history_ptr->back().second =
                            "Error: Antigravity token failed.";
                        *is_generating_flag = false;
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
            // Use Tools module to build definitions
            ai::ToolSet tools = Tools::build_definitions(
                ToolConfig{true, true});

            ai::GenerateOptions options;
            options.model = model;
            options.system = system_prompt;
            options.messages = messages;
            options.tools = tools;
            options.max_steps = 10;

            options.on_tool_call_start = [=](const ai::ToolCall& call) {
                screen->Post(
                    [=]() {
                        chat_history_ptr->push_back(
                            {"System",
                             Tools::format_tool_call(
                                 call.tool_name, call.arguments.dump())});
                        screen->Post(ftxui::Event::Custom);
                    });
            };

            options.on_tool_call_finish = [=](const ai::ToolResult& res) {
                screen->Post([=]() {
                    chat_history_ptr->push_back(
                        {"System",
                         Tools::format_tool_result(
                             res.tool_name, res.is_success(),
                             res.is_success() ? res.result.dump()
                                              : res.error_message())});
                    screen->Post(ftxui::Event::Custom);
                });
            };

            auto gen_result = client.generate_text(options);
            screen->Post(
                [is_generating_flag, chat_history_ptr, messages_history_ptr,
                 total_prompt_tokens_ptr, total_completion_tokens_ptr,
                 total_tokens_ptr, gen_result, screen]() {
                    *is_generating_flag = false;
                    if (gen_result.is_success()) {
                        chat_history_ptr->push_back(
                            {"Assistant", gen_result.text});
                        messages_history_ptr->push_back(
                            ai::Message::assistant(gen_result.text));
                        *total_prompt_tokens_ptr +=
                            gen_result.usage.prompt_tokens;
                        *total_completion_tokens_ptr +=
                            gen_result.usage.completion_tokens;
                        *total_tokens_ptr += gen_result.usage.total_tokens;
                    } else
                        chat_history_ptr->push_back(
                            {"System",
                             "Error: " + gen_result.error_message()});
                    screen->Post(ftxui::Event::Custom);
                });
        } else {
            ai::GenerateOptions gen_options;
            gen_options.model = model;
            gen_options.system = system_prompt;
            gen_options.messages = messages;
            ai::StreamOptions stream_options(std::move(gen_options));
            auto stream = client.stream_text(stream_options);

            if (stream.has_error()) {
                screen->Post(
                    [chat_history_ptr, is_generating_flag,
                     screen, err_msg = stream.error_message()]() {
                        chat_history_ptr->back().second =
                            "Error: " + err_msg;
                        *is_generating_flag = false;
                        screen->Post(ftxui::Event::Custom);
                    });
                return;
            }

            for (const auto& event : stream) {
                if (event.is_text_delta()) {
                    screen->Post(
                        [chat_history_ptr, screen,
                         text = event.text_delta]() {
                            chat_history_ptr->back().second += text;
                            screen->Post(ftxui::Event::Custom);
                        });
                } else if (event.is_error()) {
                    screen->Post(
                        [chat_history_ptr, screen]() {
                            chat_history_ptr->back().second +=
                                "\n[Error]";
                            screen->Post(ftxui::Event::Custom);
                        });
                    break;
                } else if (event.is_finish() && event.usage.has_value()) {
                    auto u = event.usage.value();
                    screen->Post(
                        [total_prompt_tokens_ptr,
                         total_completion_tokens_ptr, total_tokens_ptr,
                         u]() {
                            *total_prompt_tokens_ptr += u.prompt_tokens;
                            *total_completion_tokens_ptr +=
                                u.completion_tokens;
                            *total_tokens_ptr += u.total_tokens;
                        });
                }
            }
            screen->Post(
                [is_generating_flag, messages_history_ptr,
                 chat_history_ptr, screen]() {
                    *is_generating_flag = false;
                    messages_history_ptr->push_back(
                        ai::Message::assistant(
                            chat_history_ptr->back().second));
                    screen->Post(ftxui::Event::Custom);
                });
        }
    } catch (const std::exception& e) {
        std::string err_msg = e.what();
        screen->Post(
            [chat_history_ptr, is_generating_flag, screen,
             err_msg]() {
                chat_history_ptr->back().second =
                    "Exception: " + err_msg;
                *is_generating_flag = false;
                screen->Post(ftxui::Event::Custom);
            });
    }
}

} // namespace tui
} // namespace ai
