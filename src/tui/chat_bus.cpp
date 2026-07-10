#include <ai/tui/chat_bus.h>
#include <ai/tui/config.h>
#include <ai/tui/tools.h>
#include <ai/tui/db.h>
#include <ai/tui/contract/event.h>

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

using namespace contract;

// ──────────────────────────────────────────────────────────────
//  Tools-enabled generation path (bus version)
// ──────────────────────────────────────────────────────────────
static void run_tools_generation_bus(ai::Client& client,
                                      ai::GenerateOptions options,
                                      bus::BusPort& bus,
                                      GenerationContext& ctx) {
  auto assistant_text    = std::make_shared<std::string>();
  auto assistant_msg_idx = std::make_shared<int>(-1);
  auto tool_starts       = std::make_shared<
      std::map<std::string, std::chrono::steady_clock::time_point>>();
  auto step_counter      = std::make_shared<std::atomic<int>>(0);
  int  max_steps         = options.max_steps;

  // ── Step finished: emit MessageDelta ──
  options.on_step_finish =
      [&bus, &ctx, assistant_text](const ai::GenerateStep& step) {
        LOG_DEBUG("chat_bus: on_step_finish text_len={}", step.text.size());
        if (step.text.empty()) return;
        if (*assistant_text == "  \u23f3 Working...") {
          *assistant_text = step.text;
        } else {
          *assistant_text += step.text;
        }
        bus.publish<MessageDelta>({
            .session_id = ctx.session_id,
            .text = *assistant_text,
            .done = false
        });
      };

  // ── Tool call started: emit ToolCallStarted ──
  options.on_tool_call_start =
      [&bus, &ctx, tool_starts, step_counter, max_steps](const ai::ToolCall& call) {
        LOG_DEBUG("chat_bus: on_tool_call_start tool={} step={}/{}", call.tool_name, (int)*step_counter, max_steps);
        auto now = std::chrono::steady_clock::now();
        (*tool_starts)[call.id] = now;
        (*step_counter)++;

        bus.publish<ToolCallStarted>({
            .session_id = ctx.session_id,
            .tool_call_id = call.id,
            .tool_name = call.tool_name,
            .arguments = call.arguments
        });
      };

  // ── Tool call finished: emit ToolCallCompleted ──
  options.on_tool_call_finish =
      [&bus, &ctx, assistant_text, tool_starts](const ai::ToolResult& res) {
        double duration_s = 0.0;
        {
          auto start_it_tmp = tool_starts->find(res.tool_call_id);
          if (start_it_tmp != tool_starts->end()) {
            double d = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_it_tmp->second).count();
            LOG_DEBUG("chat_bus: on_tool_call_finish tool={} success={} duration={:.1f}s", res.tool_name, res.is_success(), d);
          } else {
            LOG_DEBUG("chat_bus: on_tool_call_finish tool={} success={} duration=unknown", res.tool_name, res.is_success());
          }
        }
        auto start_it = tool_starts->find(res.tool_call_id);
        if (start_it != tool_starts->end()) {
          duration_s = std::chrono::duration<double>(
              std::chrono::steady_clock::now() - start_it->second).count();
        }

        if (*assistant_text == "  \u23f3 Working...") {
          *assistant_text = "";
        }

        ctx.tool_call_count++;
        ctx.total_tool_time_ms += duration_s * 1000.0;

        bus.publish<ToolCallCompleted>({
            .session_id = ctx.session_id,
            .tool_call_id = res.tool_call_id,
            .tool_name = res.tool_name,
            .result = res.result,
            .is_error = !res.is_success(),
            .duration_ms = duration_s * 1000.0
        });
      };

  // ── Multi-step generation loop ──
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
  // Track whether the previous step had tool calls so we know when the model
  // is done using tools and wants to produce a final text-only response.
  bool prev_step_had_tool_calls = false;

  LOG_DEBUG("run_tools_generation_bus: starting loop max_steps={}", options.max_steps);
  while (step < options.max_steps && !finished) {
    step_messages.erase(std::next(step_messages.begin(), initial_count), step_messages.end());
    step_messages.insert(step_messages.end(), response_messages.begin(), response_messages.end());
    step_opts.messages = step_messages;
    LOG_DEBUG("run_tools_generation_bus: step={} messages={}", step, step_messages.size());
    step_opts.max_steps = 1;

    // Only enter the final (tools-cleared) streaming path when the model
    // stopped requesting tool calls after previously using them — i.e. the
    // model wants to give its final text answer after all tool work is done.
    bool is_final_step = (step > 0 && !prev_step_had_tool_calls);

    if (is_final_step) {
      ai::StreamOptions stream_opts(step_opts);
      stream_opts.tools.clear();

      LOG_DEBUG("run_tools_generation_bus: step={} streaming with tools_cleared=true", step);
      auto stream = client.stream_text(stream_opts);
      if (stream.has_error()) {
        LOG_ERROR("run_tools_generation_bus: step={} stream error: {}", step, stream.error_message());
        is_final_step = false;
      } else {
        std::string final_step_text;
        for (const auto& event : stream) {
          if (event.is_text_delta()) {
            final_step_text += event.text_delta;
            if (*assistant_text == "  \u23f3 Working...") {
              *assistant_text = event.text_delta;
            } else {
              *assistant_text += event.text_delta;
            }
            bus.publish<MessageDelta>({
                .session_id = ctx.session_id,
                .text = *assistant_text,
                .done = false
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
        LOG_DEBUG("run_tools_generation_bus: step={} stream finished text_len={}", step, final_step_text.size());
        gen_result.text += final_step_text;
        response_messages.push_back(ai::Message::assistant(final_step_text));
        finished = true;
        break;
      }
    }

    if (!is_final_step) {
      LOG_DEBUG("run_tools_generation_bus: step={} sync generate_text", step);
      ai::GenerateResult step_res = client.generate_text(step_opts);
      if (!step_res.is_success()) {
        LOG_ERROR("run_tools_generation_bus: step={} generate_text failed: finish_reason={} error=\"{}\" provider_metadata_size={}", step, step_res.finishReasonToString(), step_res.error_message(), step_res.provider_metadata.value_or("").size());
        gen_result.error = step_res.error;
        gen_result.provider_metadata = step_res.provider_metadata;
        break;
      }

      gen_result.text += step_res.text;
      gen_result.usage.prompt_tokens += step_res.usage.prompt_tokens;
      gen_result.usage.completion_tokens += step_res.usage.completion_tokens;
      gen_result.usage.total_tokens += step_res.usage.total_tokens;
      gen_result.finish_reason = step_res.finish_reason;
      gen_result.id = step_res.id;
      gen_result.model = step_res.model;
      gen_result.created = step_res.created;
      gen_result.system_fingerprint = step_res.system_fingerprint;

      LOG_DEBUG("run_tools_generation_bus: step={} has_tool_calls={} text_len={}", step, step_res.has_tool_calls(), step_res.text.size());
      if (step_res.has_tool_calls()) {
        std::vector<ai::ToolCallContentPart> tool_parts;
        for (const auto& call : step_res.tool_calls) {
          tool_parts.emplace_back(call.id, call.tool_name, call.arguments,
                                  call.thought_signature);
          gen_result.tool_calls.push_back(call);
        }
        response_messages.push_back(ai::Message::assistant_with_tools(step_res.text, tool_parts));

        std::vector<ai::ToolResultContentPart> result_parts;
        for (const auto& res : step_res.tool_results) {
          result_parts.emplace_back(res.tool_call_id, res.result, !res.is_success());
          gen_result.tool_results.push_back(res);
        }
        response_messages.push_back(ai::Message::tool_results(result_parts));

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
        LOG_DEBUG("run_tools_generation_bus: step={} no tool calls, finishing", step);
        response_messages.push_back(ai::Message::assistant(step_res.text));
        if (options.on_step_finish) {
          ai::GenerateStep step_data;
          step_data.text = step_res.text;
          step_data.finish_reason = step_res.finish_reason;
          step_data.usage = step_res.usage;
          options.on_step_finish.value()(step_data);
        }
        finished = true;
      }

      // Update tracking for the next iteration's is_final_step decision
      prev_step_had_tool_calls = step_res.has_tool_calls();
    }
    step++;
  }

  LOG_DEBUG("run_tools_generation_bus: loop complete steps={} total_text_len={} tool_calls={}", 
           step, gen_result.text.size(), gen_result.tool_calls.size());
  gen_result.response_messages = response_messages;

  bus.publish<SessionStatusChanged>({
      .session_id = ctx.session_id,
      .status = "idle"
  });

  if (gen_result.is_success()) {
    std::string final_text;
    if (!assistant_text->empty()) final_text = *assistant_text;
    else final_text = gen_result.text;

    LOG_DEBUG("run_tools_generation_bus: final assistant_text empty={} gen_result.text empty={}",
             assistant_text->empty(), gen_result.text.empty());
    bool is_placeholder = (final_text == "  \u23f3 Working...");
    if (!finished) {
      // Tool loop hit the step cap without a natural finish (model kept
      // requesting tools). Surface a clear, non-fatal warning instead of
      // presenting the pending placeholder as a successful answer.
      std::string limit_msg = "Stopped: tool loop reached the maximum step limit ("
          + std::to_string(options.max_steps)
          + ") without producing a final response. The model may be stuck "
            "requesting tools.";
      LOG_WARN("run_tools_generation_bus: {}", limit_msg);
      bus.publish<ErrorOccurred>({
          .session_id = ctx.session_id,
          .message = limit_msg,
          .severity = "warning"
      });
      if (!is_placeholder && !final_text.empty()) {
        bus.publish<MessageDelta>({
            .session_id = ctx.session_id,
            .text = final_text,
            .done = true
        });
      }
    } else if (!final_text.empty() && !is_placeholder) {
      LOG_DEBUG("run_tools_generation_bus: publishing final MessageDelta text_len={}", final_text.size());
      bus.publish<MessageDelta>({
          .session_id = ctx.session_id,
          .text = final_text,
          .done = true
      });
    } else if (!final_text.empty() && is_placeholder) {
      // Model left only the in-progress placeholder; treat as no real text.
      LOG_WARN("run_tools_generation_bus: model returned only the in-progress placeholder");
      bus.publish<ErrorOccurred>({
          .session_id = ctx.session_id,
          .message = "The model returned an empty response (no text generated).",
          .severity = "warning"
      });
    } else {
      // LLM produced no text and no tool output. Surface a non-error notice
      // instead of leaving the user with a silently blank turn.
      LOG_WARN("run_tools_generation_bus: model returned empty response (no text, no tool output)");
      bus.publish<ErrorOccurred>({
          .session_id = ctx.session_id,
          .message = "The model returned an empty response (no text generated).",
          .severity = "warning"
      });
    }

  } else if (!assistant_text->empty() || !gen_result.text.empty()) {
    LOG_WARN("run_tools_generation_bus: partial result after step failure - finishing gracefully");
    std::string final_text;
    if (!assistant_text->empty()) final_text = *assistant_text;
    else final_text = gen_result.text;
    bus.publish<MessageDelta>({
        .session_id = ctx.session_id,
        .text = final_text,
        .done = true
    });
  } else {
    std::string err_str = "Error: " + gen_result.error_message();
    if (gen_result.provider_metadata.has_value() && !gen_result.provider_metadata->empty()) {
      err_str += "\n[Response] " + gen_result.provider_metadata.value().substr(0, 500);
    }
    bus.publish<ErrorOccurred>({
        .session_id = ctx.session_id,
        .message = err_str,
        .severity = "error"
    });
  }

  bus.publish<TokenUsageUpdated>({
      .prompt_tokens = gen_result.usage.prompt_tokens,
      .completion_tokens = gen_result.usage.completion_tokens,
      .total_tokens = gen_result.usage.total_tokens
  });
}

// ──────────────────────────────────────────────────────────────
//  Non-tools streaming generation path (bus version)
// ──────────────────────────────────────────────────────────────
static void run_stream_generation_bus(ai::Client& client,
                                       ai::GenerateOptions gen_options,
                                       bus::BusPort& bus,
                                       GenerationContext& ctx) {
  ai::StreamOptions stream_options(std::move(gen_options));
  auto stream = client.stream_text(stream_options);
  LOG_DEBUG("run_stream_generation_bus: streaming model={} system={}", stream_options.model, stream_options.system.size());

  if (stream.has_error()) {
    LOG_ERROR("ChatBus: stream error: {}", stream.error_message());
    bus.publish<ErrorOccurred>({
        .session_id = ctx.session_id,
        .message = stream.error_message(),
        .severity = "error"
    });
    return;
  }

  std::string text_buffer;        // per-flush throttle accumulator
  std::string full_text;           // complete assistant transcript (never cleared)
  auto last_flush = std::chrono::steady_clock::now();
  constexpr auto FLUSH_INTERVAL = std::chrono::milliseconds(33);

  auto flush_text = [&]() {
    if (text_buffer.empty()) return;
    LOG_DEBUG("ChatBus: flush_text buffer_size={}", text_buffer.size());
    std::string snap = full_text;
    text_buffer.clear();
    bus.publish<MessageDelta>({
        .session_id = ctx.session_id,
        .text = snap,
        .done = false
    });
  };

  std::string reasoning_buffer;     // per-flush throttle accumulator
  std::string full_reasoning;       // complete reasoning transcript (never cleared)
  std::string last_reasoning_signature;
  auto flush_reasoning = [&]() {
    if (reasoning_buffer.empty()) return;
    LOG_DEBUG("ChatBus: flush_reasoning buffer_size={}", reasoning_buffer.size());
    std::string snap = full_reasoning;
    reasoning_buffer.clear();
    bus.publish<ReasoningDelta>({
        .session_id = ctx.session_id,
        .text = snap,
        .signature = last_reasoning_signature,
        .done = false
    });
  };

  for (const auto& event : stream) {
    if (event.is_text_delta()) {
      text_buffer += event.text_delta;
      full_text   += event.text_delta;
      LOG_DEBUG("run_stream_generation_bus: text_delta buffer_size={}", text_buffer.size());
      auto now = std::chrono::steady_clock::now();
      if (now - last_flush >= FLUSH_INTERVAL) {
        flush_text();
        last_flush = now;
      }
    } else if (event.is_reasoning_delta()) {
      // OpenRouter encrypts reasoning as [REDACTED]; drop those chunks.
      std::string chunk = event.text_delta;
      if (chunk.find("[REDACTED]") != std::string::npos) {
        LOG_DEBUG("ChatBus: dropping redacted reasoning chunk");
      } else {
        reasoning_buffer += chunk;
        full_reasoning  += chunk;
        if (event.metadata.has_value() && !event.metadata->empty()) {
          last_reasoning_signature = *event.metadata;
        }
        LOG_DEBUG("ChatBus: reasoning_delta buffer_size={}", reasoning_buffer.size());
        auto now = std::chrono::steady_clock::now();
        if (now - last_flush >= FLUSH_INTERVAL) {
          flush_reasoning();
          last_flush = now;
        }
      }
    } else if (event.is_error()) {
      LOG_ERROR("run_stream_generation_bus: stream error");
      flush_text();
      flush_reasoning();
      bus.publish<ErrorOccurred>({
          .session_id = ctx.session_id,
          .message = "Error during streaming",
          .severity = "error"
      });
      return;  // do NOT publish a success message / save / go idle after an error
    } else if (event.is_finish() && event.usage.has_value()) {
      LOG_DEBUG("run_stream_generation_bus: stream finished text_len={}", text_buffer.size());
      flush_text();
      bus.publish<TokenUsageUpdated>({
          .prompt_tokens = event.usage->prompt_tokens,
          .completion_tokens = event.usage->completion_tokens,
          .total_tokens = event.usage->total_tokens
      });
    }
  }

  LOG_DEBUG("run_stream_generation_bus: publishing final MessageDelta text_len={}", full_text.size());
  bus.publish<MessageDelta>({
      .session_id = ctx.session_id,
      .text = full_text,
      .done = true
  });

  if (!full_reasoning.empty()) {
    bus.publish<ReasoningDelta>({
        .session_id = ctx.session_id,
        .text = full_reasoning,
        .signature = last_reasoning_signature,
        .done = true
    });
  }

  bus.publish<SessionStatusChanged>({
      .session_id = ctx.session_id,
      .status = "idle"
  });
}

// ──────────────────────────────────────────────────────────────
//  Public entry point — matches the client creation pattern
//  from the original run_llm_generation in chat.cpp
// ──────────────────────────────────────────────────────────────
void run_generation_with_bus(
    const std::string& provider_name,
    const std::string& model_id,
    const std::string& system_prompt,
    const ai::Messages& messages,
    bool enable_tools,
    const std::vector<ProviderInfo>& providers,
    bus::BusPort& bus,
    GenerationContext& ctx)
{
  try {
    bus.publish<SessionStatusChanged>({
        .session_id = ctx.session_id,
        .status = "generating"
    });

    // ── Resolve provider & API key ──
    ai::Client client;
    std::string provider_id;
    ai::providers::ProviderOptions provider_options;

    for (const auto& p : providers) {
      if (p.id == provider_name || p.name == provider_name) {
        provider_id = p.id;
        provider_options.base_url = p.api_url;
        provider_options.api_key = p.api_key;
        provider_options.headers = p.headers;
        provider_options.protocol = p.protocol;
        provider_options.project_id = p.project_id;
        break;
      }
    }

    LOG_DEBUG("chat_bus: resolved provider={} api_url={}", provider_id,
              provider_options.base_url);

    // Provider registry is populated once at startup (see main.cpp). Resolve the
    // provider client via the central registry (auth + base URL handled per
    // provider). Errors are surfaced on the bus.
    auto resolution =
        ai::providers::ProviderRegistry::instance().resolve(provider_id,
                                                             provider_options);
    if (!resolution.ok()) {
      bus.publish<ErrorOccurred>({
          .session_id = ctx.session_id,
          .message = resolution.error,
          .severity = "error"
      });
      return;
    }
    client = std::move(resolution.client);

    LOG_INFO("ChatBus: provider={}, model={}, tools={}", provider_id, model_id, enable_tools);

    // ── Build common base options ──
    ai::GenerateOptions base_opts;
    base_opts.model = model_id;
    base_opts.system = system_prompt;
    base_opts.messages = messages;

    // ── Opt-in extended thinking / reasoning ──
    const std::string& rm = ctx.reasoning_mode;
    if (rm == "low" || rm == "medium" || rm == "high") {
      bool is_anthropic =
          (provider_id.find("anthropic") != std::string::npos) ||
          (model_id.find("claude") != std::string::npos);
      if (is_anthropic) {
        int budget = (rm == "low") ? 2000 : (rm == "medium") ? 8000 : 16000;
        base_opts.budget_tokens = budget;
      } else {
        base_opts.reasoning_effort = rm;  // openai o-series / openrouter / etc.
      }
    }

    // ── Dispatch ──
    if (enable_tools) {
      ai::ToolSet tools = Tools::build_definitions(ToolConfig{true, true});
      base_opts.tools = std::move(tools);
      constexpr int kMaxToolSteps = 200;  // bounded safe default (was 99999999)
      base_opts.max_steps = kMaxToolSteps;
      run_tools_generation_bus(client, std::move(base_opts), bus, ctx);
    } else {
      run_stream_generation_bus(client, std::move(base_opts), bus, ctx);
    }

  } catch (const std::exception& e) {
    std::string err_msg = e.what();
    bus.publish<ErrorOccurred>({
        .session_id = ctx.session_id,
        .message = "Exception: " + err_msg,
        .severity = "error"
    });
  }
}

} // namespace tui
} // namespace ai
