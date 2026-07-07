#include <ai/tui/chat.h>
#include <ai/tui/config.h>
#include <ai/tui/tools.h>
#include <ai/tui/db.h>
#include <atomic>

#include <chrono>
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

        LOG_INFO("LLM gen: provider={}, model={}, tools={}",
                             provider_id, model, enable_tools);

        if (enable_tools) {
            ai::ToolSet tools = Tools::build_definitions(ToolConfig{true, true});

            ai::GenerateOptions options;
            options.model = model;
            options.system = system_prompt;
            options.messages = messages;
            options.tools = tools;
            options.max_steps = 99999999;

            // ── Tool observability: timing + step counter + streaming ──
            auto tool_starts = std::make_shared<std::map<std::string, std::chrono::steady_clock::time_point>>();
            auto step_counter = std::make_shared<std::atomic<int>>(0);
            int max_steps = options.max_steps;

            // Track progressive assistant text across steps
            auto assistant_text = std::make_shared<std::string>();
            auto assistant_msg_idx = std::make_shared<int>(-1);

            options.on_step_finish = [state, screen, assistant_text, assistant_msg_idx](const ai::GenerateStep& step) {
                if (!step.text.empty()) {
                LOG_DEBUG("Chat: step_finish text_size={}", step.text.size());
                    *assistant_text += step.text;
                    screen->Post([state, screen, assistant_text, assistant_msg_idx, text = *assistant_text]() {
                        if (*assistant_msg_idx < 0) {
                            // First time: push an Assistant entry
                            state.chat_history->push_back({"Assistant", text});
                            *assistant_msg_idx = static_cast<int>(state.chat_history->size()) - 1;
                        } else if (*assistant_msg_idx < static_cast<int>(state.chat_history->size())) {
                            // Update existing Assistant entry
                            (*state.chat_history)[*assistant_msg_idx].second = text;
                        }
                        screen->Post(ftxui::Event::Custom);
                    });
                }
            };

            options.on_tool_call_start = [state, screen, tool_starts, step_counter, max_steps, assistant_text, assistant_msg_idx](const ai::ToolCall& call) {
                LOG_DEBUG("Chat: tool_call_start tool={}", call.tool_name);
                (*tool_starts)[call.id] = std::chrono::steady_clock::now();
                (*step_counter)++;
                int step = *step_counter;
                screen->Post([state, screen, call, step, max_steps, assistant_text, assistant_msg_idx]() {
                    // Push a live Assistant placeholder if none exists yet
                    if (*assistant_msg_idx < 0 && assistant_text->empty()) {
                        *assistant_text = "  \u23f3 Working...";
                        state.chat_history->push_back({"Assistant", *assistant_text});
                        *assistant_msg_idx = static_cast<int>(state.chat_history->size()) - 1;
                    }
                    std::string tool_call_str = Tools::format_tool_call(
                        call.tool_name, call.arguments.dump(), step, max_steps);
                    state.chat_history->push_back({"ToolCall", tool_call_str});
                    state.chat_history->push_back(
                        {"ToolResult", "  \u23f3 Running " + call.tool_name + "..."});
                    ai::tui::db::save_message(*state.session_id, "ToolCall", tool_call_str);
                    screen->Post(ftxui::Event::Custom);
                });
            };

            options.on_tool_call_finish = [state, screen, tool_starts, assistant_text](const ai::ToolResult& res) {
                screen->Post([state, screen, res, tool_starts, assistant_text]() {
                    // Clear "Working..." placeholder once first tool result arrives
                    if (assistant_text && *assistant_text == "  \u23f3 Working...") {
                        *assistant_text = "";
                    }
                    auto end_time = std::chrono::steady_clock::now();
                    // Look up this tool's start time from the map
                    double duration_s = 0.0;
                    auto start_it = tool_starts->find(res.tool_call_id);
                    if (start_it != tool_starts->end()) {
                        duration_s = std::chrono::duration<double>(
                            end_time - start_it->second).count();
                    }
                    LOG_DEBUG("Chat: tool_call_finish tool={} success={} duration={:.2f}s", res.tool_name, res.is_success(), duration_s);

                    // Find and remove the specific "Running..." entry for this tool
                    // by searching backwards for the right tool_name
                    for (int ci = static_cast<int>(state.chat_history->size()) - 1; ci >= 0; ci--) {
                        auto& entry = (*state.chat_history)[ci];
                        if (entry.first == "ToolResult" &&
                            entry.second.find("Running") != std::string::npos &&
                            entry.second.find(res.tool_name) != std::string::npos) {
                            state.chat_history->erase(state.chat_history->begin() + ci);
                            break;
                        }
                    }

                    std::string tool_res_str = Tools::format_tool_result(
                        res.tool_name, res.is_success(),
                        res.is_success() ? res.result.dump()
                                         : res.error_message(),
                        500, duration_s);

                    state.chat_history->push_back({"ToolResult", tool_res_str});

                    // Update tool observability stats
                    (*state.tool_call_count)++;
                    *state.total_tool_time_ms += duration_s * 1000.0;

                    ai::tui::db::save_message(*state.session_id, "ToolResult", tool_res_str);
                    screen->Post(ftxui::Event::Custom);
                });
            };

            auto gen_result = client.generate_text(options);
            screen->Post(
                [state, gen_result, assistant_text, assistant_msg_idx, screen]() {
                    *state.is_generating = false;
                    if (gen_result.is_success()) {
                        // If we already streamed step text, update the final entry
                        // otherwise push a new Assistant message
                        std::string final_text = gen_result.text;
                        if (!assistant_text->empty()) {
                            final_text = *assistant_text;
                            // Update existing entry if not already done
                            if (*assistant_msg_idx >= 0 &&
                                *assistant_msg_idx < static_cast<int>(state.chat_history->size())) {
                                (*state.chat_history)[*assistant_msg_idx].second = final_text;
                                // Save to DB
                                ai::tui::db::save_message(*state.session_id, "Assistant", final_text);
                                screen->Post(ftxui::Event::Custom);
                                return;
                            }
                        }
                        if (final_text.empty()) {
                            final_text = "  \u2705 Done";
                        }
                        state.chat_history->push_back(
                            {"Assistant", final_text});
                        ai::tui::db::save_message(*state.session_id, "Assistant", final_text);
                    } else {
                        std::string err_str = "Error: " + gen_result.error_message();
                        state.chat_history->push_back({"System", err_str});
                        ai::tui::db::save_message(*state.session_id, "System", err_str);
                    }

                    // Maintain the complete conversation history including assistant tool calls + tool results
                    if (gen_result.is_success() && !gen_result.response_messages.empty()) {
                        state.messages_history->insert(
                            state.messages_history->end(),
                            gen_result.response_messages.begin(),
                            gen_result.response_messages.end()
                        );
                    } else if (gen_result.is_success()) {
                        state.messages_history->push_back(
                            ai::Message::assistant(gen_result.text));
                    }

                    *state.total_prompt_tokens +=
                        gen_result.usage.prompt_tokens;
                    *state.total_completion_tokens +=
                        gen_result.usage.completion_tokens;
                    *state.total_tokens += gen_result.usage.total_tokens;
                    screen->Post(ftxui::Event::Custom);
                });
        } else {
            // Push Assistant placeholder synchronously BEFORE stream starts
            state.chat_history->push_back({"Assistant", ""});

            ai::GenerateOptions gen_options;
            gen_options.model = model;
            gen_options.system = system_prompt;
            gen_options.messages = messages;
            ai::StreamOptions stream_options(std::move(gen_options));
            auto stream = client.stream_text(stream_options);
            LOG_DEBUG("Chat: stream_text opened successfully");

            if (stream.has_error()) {
                LOG_ERROR("Chat: stream error: {}", stream.error_message());
                screen->Post(
                    [state, screen, err_msg = stream.error_message()]() {
                        state.chat_history->back().second = "Error: " + err_msg;
                        *state.is_generating = false;
                        ai::tui::db::save_message(*state.session_id, "System", "Error: " + err_msg);
                        screen->Post(ftxui::Event::Custom);
                    });
                return;
            }

            // Throttled streaming: batch deltas at ~30fps max
            std::string text_buffer;
            auto last_flush = std::chrono::steady_clock::now();
            constexpr auto FLUSH_INTERVAL = std::chrono::milliseconds(33);

            auto flush_text = [&]() {
                LOG_DEBUG("Chat: flush_text called, buffer={}", text_buffer.size());
                if (!text_buffer.empty()) {
                    std::string batch = std::move(text_buffer);
                    text_buffer.clear();
                    screen->Post(
                        [chat_history_ptr = state.chat_history, screen,
                         batch = std::move(batch)]() {
                            if (chat_history_ptr->empty()) {
                                chat_history_ptr->push_back({"Assistant", batch});
                            } else {
                                chat_history_ptr->back().second += batch;
                            }
                            screen->Post(ftxui::Event::Custom);
                        });
                }
            };

            for (const auto& event : stream) {
                if (event.is_text_delta()) {
                    text_buffer += event.text_delta;
                    if (text_buffer.size() % 50 == 0) {
                        LOG_DEBUG("Chat: streaming buffer_size={}", text_buffer.size());
                    }
                    auto now = std::chrono::steady_clock::now();
                    if (now - last_flush >= FLUSH_INTERVAL) {
                        flush_text();
                        last_flush = now;
                    }
                } else if (event.is_error()) {
                    LOG_ERROR("Chat: stream error during generation");
                    flush_text();
                    screen->Post(
                        [state, screen]() {
                            state.chat_history->back().second += "\n[Error]";
                            ai::tui::db::save_message(*state.session_id, "System", "Error during streaming");
                            screen->Post(ftxui::Event::Custom);
                        });
                    break;
                } else if (event.is_finish() && event.usage.has_value()) {
                    LOG_DEBUG("Chat: stream finished, flushing final text");
                    flush_text();
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
            LOG_DEBUG("Chat: final flush after stream loop");
            flush_text(); // flush any remaining text
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
