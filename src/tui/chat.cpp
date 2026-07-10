#include <ai/tui/chat.h>
#include <ai/tui/config.h>
#include <ai/tui/tools.h>
#include <ai/tui/db.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <string>
#include <utility>

#include <ai/types/client.h>
#include <ai/logger.h>
#include <ai/openai.h>
#include "ai/registry.h"
#include "ai/tui/provider_registry_init.h"

namespace ai {
namespace tui {

// ──────────────────────────────────────────────────────────────
//  Internal helpers
// ──────────────────────────────────────────────────────────────

/** Post a function to the UI thread and block until it completes. */
static void post_sync(ftxui::ScreenInteractive* screen,
                      std::function<void()> fn) {
  auto done = std::make_shared<std::promise<void>>();
  auto future = done->get_future();
  screen->Post(
      [fn = std::move(fn), done]() {
        fn();
        done->set_value();
      });
  future.wait();
}

static void report_error(ftxui::ScreenInteractive* screen, ChatState state,
                         const std::string& msg) {
  screen->Post([state, screen, msg]() {
    state.chat_history->push_back({"System", "Error: " + msg});
    *state.is_generating = false;
    ai::tui::db::save_message(*state.session_id, "System", "Error: " + msg);
    screen->Post(ftxui::Event::Custom);
  });
}

// ──────────────────────────────────────────────────────────────
//  Tools-enabled generation path
// ──────────────────────────────────────────────────────────────
static void run_tools_generation(ai::Client& client,
                                  ai::GenerateOptions options,
                                  ftxui::ScreenInteractive* screen,
                                  ChatState state) {
  // ── Shared state (mutated only on the UI thread via screen->Post) ──
  auto assistant_text   = std::make_shared<std::string>();
  auto assistant_msg_idx = std::make_shared<int>(-1);
  auto tool_starts      = std::make_shared<
      std::map<std::string, std::chrono::steady_clock::time_point>>();
  auto step_counter     = std::make_shared<std::atomic<int>>(0);
  int  max_steps        = options.max_steps;

  // ── Step finished: incremental text accumulation ──
  options.on_step_finish =
      [=](const ai::GenerateStep& step) {
        if (step.text.empty()) return;
        screen->Post([=, increment = step.text]() {
          // If the assistant entry still shows the "Working…" placeholder,
          // replace it with the real text instead of appending.
          if (*assistant_text == "  \u23f3 Working...") {
            *assistant_text = increment;
          } else {
            *assistant_text += increment;
          }

          if (*assistant_msg_idx < 0) {
            state.chat_history->push_back({"Assistant", *assistant_text});
            *assistant_msg_idx =
                static_cast<int>(state.chat_history->size()) - 1;
          } else if (*assistant_msg_idx <
                     static_cast<int>(state.chat_history->size())) {
            (*state.chat_history)[*assistant_msg_idx].second = *assistant_text;
          }
          screen->Post(ftxui::Event::Custom);
        });
      };

  // ── Tool call started: show "Running…" placeholder ──
  options.on_tool_call_start =
      [=](const ai::ToolCall& call) {
        auto now = std::chrono::steady_clock::now();
        (*tool_starts)[call.id] = now;            // generation thread only
        (*step_counter)++;
        int step = *step_counter;

        screen->Post([=]() {
          // Ensure at least one Assistant entry exists
          if (*assistant_msg_idx < 0 && assistant_text->empty()) {
            *assistant_text = "  \u23f3 Working...";
            state.chat_history->push_back({"Assistant", *assistant_text});
            *assistant_msg_idx =
                static_cast<int>(state.chat_history->size()) - 1;
          }

          std::string tool_call_str = Tools::format_tool_call(
              call.tool_name, call.arguments.dump(), step, max_steps);
          state.chat_history->push_back({"ToolCall", tool_call_str});
          state.chat_history->push_back(
              {"ToolResult",
               "  \u23f3 Running " + call.tool_name + "..."});
          ai::tui::db::save_message(*state.session_id, "ToolCall",
                                     tool_call_str);
          screen->Post(ftxui::Event::Custom);
        });
      };

  // ── Tool call finished: replace placeholder with result ──
  options.on_tool_call_finish =
      [=](const ai::ToolResult& res) {
        // Compute duration on the generation thread to avoid racing
        // on `tool_starts`.
        double duration_s = 0.0;
        auto start_it = tool_starts->find(res.tool_call_id);
        if (start_it != tool_starts->end()) {
          duration_s = std::chrono::duration<double>(
              std::chrono::steady_clock::now() - start_it->second).count();
        }

        screen->Post([=]() {
          // If the Assistant entry is still just "Working…", clear it so that
          // later step text isn't polluted.
          if (*assistant_text == "  \u23f3 Working...") {
            *assistant_text = "";
          }

          // Remove the ephemeral "Running…" placeholder for this tool
          for (int ci = static_cast<int>(state.chat_history->size()) - 1;
               ci >= 0; --ci) {
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
              res.is_success() ? res.result.dump() : res.error_message(),
              -1, duration_s);
          state.chat_history->push_back({"ToolResult", tool_res_str});

          (*state.tool_call_count)++;
          *state.total_tool_time_ms += duration_s * 1000.0;
          ai::tui::db::save_message(*state.session_id, "ToolResult",
                                     tool_res_str);
          screen->Post(ftxui::Event::Custom);
        });
      };

  // ── Manual Multi-Step loop to allow streaming of the final text response ──
  ai::GenerateResult gen_result;
  gen_result.finish_reason = ai::kFinishReasonStop;

  ai::Messages response_messages;
  ai::Messages step_messages = options.messages;
  if (step_messages.empty() && !options.prompt.empty()) {
    step_messages.push_back(ai::Message::user(options.prompt));
  }
  const size_t initial_count = step_messages.size();

  ai::GenerateOptions step_opts = options;
  step_opts.prompt.clear();

  int step = 0;
  bool finished = false;

  while (step < options.max_steps && !finished) {
    // Rebuild the message list for this step (initial inputs + running responses)
    step_messages.erase(std::next(step_messages.begin(), initial_count), step_messages.end());
    step_messages.insert(step_messages.end(), response_messages.begin(), response_messages.end());
    step_opts.messages = step_messages;
    step_opts.max_steps = 1; // Run one step at a time

    // If the model previously executed tool calls, the final step is a synthesis response.
    // We can stream it!
    bool is_final_step = (step > 0);

    if (is_final_step) {
      // Stream final response without tools to force text streaming
      ai::StreamOptions stream_opts(step_opts);
      stream_opts.tools.clear(); // Disable tools to force streaming final text response
      
      auto stream = client.stream_text(stream_opts);
      if (stream.has_error()) {
        LOG_ERROR("run_tools_generation stream error: {}", stream.error_message());
        // Fallback to generate_text
        is_final_step = false;
      } else {
        std::string final_step_text;
        for (const auto& event : stream) {
          if (event.is_text_delta()) {
            final_step_text += event.text_delta;
            screen->Post([=, increment = event.text_delta]() {
              if (*assistant_text == "  \u23f3 Working...") {
                *assistant_text = increment;
              } else {
                *assistant_text += increment;
              }
              if (*assistant_msg_idx < 0) {
                state.chat_history->push_back({"Assistant", *assistant_text});
                *assistant_msg_idx = static_cast<int>(state.chat_history->size()) - 1;
              } else {
                (*state.chat_history)[*assistant_msg_idx].second = *assistant_text;
              }
              screen->Post(ftxui::Event::Custom);
            });
          } else if (event.is_finish()) {
            if (event.usage.has_value()) {
              auto u = event.usage.value();
              gen_result.usage.prompt_tokens += u.prompt_tokens;
              gen_result.usage.completion_tokens += u.completion_tokens;
              gen_result.usage.total_tokens += u.total_tokens;
            }
          }
        }
        
        // Finalize state
        gen_result.text += final_step_text;
        response_messages.push_back(ai::Message::assistant(final_step_text));
        finished = true;
        break;
      }
    }

    if (!is_final_step) {
      // Synchronous step (which handles tool executions automatically under the hood)
      ai::GenerateResult step_res = client.generate_text(step_opts);
      if (!step_res.is_success()) {
        gen_result.error = step_res.error;
        break;
      }

      // Roll up step metrics
      gen_result.text += step_res.text;
      gen_result.usage.prompt_tokens += step_res.usage.prompt_tokens;
      gen_result.usage.completion_tokens += step_res.usage.completion_tokens;
      gen_result.usage.total_tokens += step_res.usage.total_tokens;
      gen_result.finish_reason = step_res.finish_reason;
      gen_result.id = step_res.id;
      gen_result.model = step_res.model;
      gen_result.created = step_res.created;
      gen_result.system_fingerprint = step_res.system_fingerprint;

      // Handle step outputs: tool calls or final text
      if (step_res.has_tool_calls()) {
        std::vector<ai::ToolCallContentPart> tool_parts;
        for (const auto& call : step_res.tool_calls) {
          tool_parts.emplace_back(call.id, call.tool_name, call.arguments);
          gen_result.tool_calls.push_back(call);
        }
        response_messages.push_back(ai::Message::assistant_with_tools(step_res.text, tool_parts));

        std::vector<ai::ToolResultContentPart> result_parts;
        for (const auto& res : step_res.tool_results) {
          result_parts.emplace_back(res.tool_call_id, res.result, !res.is_success());
          gen_result.tool_results.push_back(res);
        }
        response_messages.push_back(ai::Message::tool_results(result_parts));

        // Setup options on step finish to match single-step hooks
        if (options.on_step_finish) {
          ai::GenerateStep step_data;
          step_data.text = step_res.text;
          step_data.tool_calls = step_res.tool_calls;
          step_data.tool_results = step_res.tool_results;
          step_data.finish_reason = step_res.finish_reason;
          step_data.usage = step_res.usage;
          options.on_step_finish.value()(step_data);
        }
      } else {
        // No tools requested/called. The model responded directly with text.
        // We can just append it to response_messages.
        response_messages.push_back(ai::Message::assistant(step_res.text));
        finished = true;
      }
    }

    step++;
  }

  // Populate response_messages for finalized trace
  gen_result.response_messages = response_messages;

  // ── Finalise: save final assistant text, update message history ──
  screen->Post([=]() {
    *state.is_generating = false;

    if (gen_result.is_success()) {
      // Final display text: prefer the step-accumulated text, fall back
      // to the monolithic result, then to a simple "Done" marker.
      std::string final_text;
      if (!assistant_text->empty()) {
        final_text = *assistant_text;
      } else {
        final_text = gen_result.text;
      }
      // Don't hardcode assistant text — if the LLM produced nothing,
      // the tool call/result messages already in the history suffice.
      if (final_text.empty()) {
        // Skip — no text to show; SessionStatusChanged signals completion.
      }

      // Update the single Assistant entry we created during streaming,
      // or push a new one if none exists yet (edge case).
      if (*assistant_msg_idx >= 0 &&
          *assistant_msg_idx <
              static_cast<int>(state.chat_history->size())) {
        (*state.chat_history)[*assistant_msg_idx].second = final_text;
      } else {
        state.chat_history->push_back({"Assistant", final_text});
        *assistant_msg_idx =
            static_cast<int>(state.chat_history->size()) - 1;
      }
      ai::tui::db::save_message(*state.session_id, "Assistant", final_text);

      // Keep the full conversation history for the API context
      if (!gen_result.response_messages.empty()) {
        state.messages_history->insert(state.messages_history->end(),
                                       gen_result.response_messages.begin(),
                                       gen_result.response_messages.end());
      } else {
        state.messages_history->push_back(
            ai::Message::assistant(gen_result.text));
      }
    } else {
      std::string err_str = "Error: " + gen_result.error_message();
      state.chat_history->push_back({"System", err_str});
      ai::tui::db::save_message(*state.session_id, "System", err_str);
    }

    // Token accounting
    *state.total_prompt_tokens += gen_result.usage.prompt_tokens;
    *state.total_completion_tokens += gen_result.usage.completion_tokens;
    *state.total_tokens += gen_result.usage.total_tokens;

    screen->Post(ftxui::Event::Custom);
  });
}

// ──────────────────────────────────────────────────────────────
//  Non-tools streaming generation path
// ──────────────────────────────────────────────────────────────
static void run_stream_generation(ai::Client& client,
                                   ai::GenerateOptions gen_options,
                                   ftxui::ScreenInteractive* screen,
                                   ChatState state) {
  // ── 1. Open the stream ──
  ai::StreamOptions stream_options(std::move(gen_options));
  auto stream = client.stream_text(stream_options);
  LOG_DEBUG("Chat: stream_text opened successfully");

  if (stream.has_error()) {
    LOG_ERROR("Chat: stream error: {}", stream.error_message());
    report_error(screen, state, stream.error_message());
    return;
  }

  // ── 2. Create the Assistant entry on the UI thread before streaming ──
  //     (so flush_text always has a valid .back() to append to)
  post_sync(screen, [state]() {
    state.chat_history->push_back({"Assistant", ""});
  });

  // ── 3. Throttled streaming loop (generation thread) ──
  std::string text_buffer;
  auto last_flush = std::chrono::steady_clock::now();
  constexpr auto FLUSH_INTERVAL = std::chrono::milliseconds(33);

  auto flush_text = [&]() {
    if (text_buffer.empty()) return;
    LOG_DEBUG("Chat: flush_text buffer_size={}", text_buffer.size());
    std::string batch = std::move(text_buffer);
    text_buffer.clear();
    screen->Post([chat_history_ptr = state.chat_history, screen,
                  batch = std::move(batch)]() {
      // Append to the last entry (the Assistant placeholder we created above)
      if (!chat_history_ptr->empty()) {
        chat_history_ptr->back().second += batch;
      } else {
        chat_history_ptr->push_back({"Assistant", batch});
      }
      screen->Post(ftxui::Event::Custom);
    });
  };

  for (const auto& event : stream) {
    if (event.is_text_delta()) {
      text_buffer += event.text_delta;
      auto now = std::chrono::steady_clock::now();
      if (now - last_flush >= FLUSH_INTERVAL) {
        flush_text();
        last_flush = now;
      }
    } else if (event.is_error()) {
      LOG_ERROR("Chat: stream error during generation");
      flush_text();
      screen->Post([state, screen]() {
        state.chat_history->back().second += "\n[Error]";
        ai::tui::db::save_message(*state.session_id, "System",
                                   "Error during streaming");
        screen->Post(ftxui::Event::Custom);
      });
      break;
    } else if (event.is_finish() && event.usage.has_value()) {
      LOG_DEBUG("Chat: stream finished, flushing final text");
      flush_text();
      auto u = event.usage.value();
      screen->Post([total_prompt_tokens_ptr = state.total_prompt_tokens,
                    total_completion_tokens_ptr =
                        state.total_completion_tokens,
                    total_tokens_ptr = state.total_tokens, u]() {
        *total_prompt_tokens_ptr += u.prompt_tokens;
        *total_completion_tokens_ptr += u.completion_tokens;
        *total_tokens_ptr += u.total_tokens;
      });
    }
  }

  // ── 4. Final flush + persist ──
  LOG_DEBUG("Chat: final flush after stream loop");
  flush_text();

  screen->Post([state, screen]() {
    *state.is_generating = false;
    std::string assistant_text = state.chat_history->back().second;
    state.messages_history->push_back(
        ai::Message::assistant(assistant_text));
    ai::tui::db::save_message(*state.session_id, "Assistant",
                               assistant_text);
    screen->Post(ftxui::Event::Custom);
  });
}

// ──────────────────────────────────────────────────────────────
//  Entry point
// ──────────────────────────────────────────────────────────────
void run_llm_generation(
    std::string provider,
    std::string model,
    std::string system_prompt,
    ai::Messages messages,
    bool enable_tools,
    std::vector<ProviderInfo> providers_list,
    ftxui::ScreenInteractive* screen,
    ChatState state) {
  try {
    // ── Resolve provider & API key ──
    ai::Client client;
    std::string api_url;
    std::string provider_id;

    for (const auto& p : providers_list) {
      if (p.name == provider) {
        api_url = p.api_url;
        provider_id = p.id;
        break;
      }
    }

    // Resolve the provider client via the central registry (auth + base
    // URL handled per provider). Errors are surfaced through report_error.
    ai::providers::register_tui_providers();
    auto resolution =
        ai::providers::ProviderRegistry::instance().resolve(provider_id, api_url);
    if (!resolution.ok()) {
      report_error(screen, state, resolution.error);
      return;
    }
    client = std::move(resolution.client);

    LOG_INFO("LLM gen: provider={}, model={}, tools={}", provider_id,
             model, enable_tools);

    // ── Build common base options ──
    ai::GenerateOptions base_opts;
    base_opts.model = model;
    base_opts.system = system_prompt;
    base_opts.messages = std::move(messages);

    // ── Dispatch ──
    if (enable_tools) {
      ai::ToolSet tools =
          Tools::build_definitions(ToolConfig{true, true});
      base_opts.tools = std::move(tools);
      base_opts.max_steps = 200;  // bounded safe default (was 99999999)
      run_tools_generation(client, std::move(base_opts), screen, state);
    } else {
      run_stream_generation(client, std::move(base_opts), screen, state);
    }

  } catch (const std::exception& e) {
    std::string err_msg = e.what();
    screen->Post([state, screen, err_msg]() {
      state.chat_history->push_back({"System", "Exception: " + err_msg});
      *state.is_generating = false;
      ai::tui::db::save_message(*state.session_id, "System",
                                 "Exception: " + err_msg);
      screen->Post(ftxui::Event::Custom);
    });
  }
}

}  // namespace tui
}  // namespace ai
