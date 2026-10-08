#include <qcode/generation/call_usage.h>

#include <qcode/core/event.h>
#include <qcode/session/session_store.h>

#include <algorithm>
#include <utility>

namespace qcode {

std::string usage_model_id(const ModelInfo* model_info,
                           const std::string& wire_model) {
  return model_info != nullptr ? model_info->id : wire_model;
}

session::ModelCallUsage model_call_usage(const GenerateOptions& request,
                                         const GenerateResult& result,
                                         const std::string& provider_id,
                                         const ModelInfo* model_info,
                                         double model_ms) {
  int think = result.usage.reasoning_completion_tokens;
  if (think == 0 && !result.reasoning.empty()) {
    think = std::max(1, static_cast<int>(result.reasoning.size() / 4));
  }
  return {.model_ms = model_ms,
          .ttft_ms = result.ttft_ms.value_or(-1.0),
          .input_tokens = result.usage.prompt_tokens,
          .cache_read_tokens = result.usage.cached_prompt_tokens,
          .cache_write_tokens = result.usage.cache_write_tokens,
          .output_tokens = result.usage.completion_tokens,
          .reasoning_tokens = think,
          .effort = request.reasoning_effort.value_or("off"),
          .variant = request.reasoning_variant.value_or(""),
          .provider = provider_id,
          .model = usage_model_id(model_info, request.model),
          .cost = {}};
}

void record_model_call(bus::BusPort* bus, const std::string& session_id,
                       const ModelInfo* model_info, session::ModelCallUsage call,
                       int step, bool streamed, bool ok) {
  if (model_info != nullptr) call.cost = session::price_call(call, *model_info);
  session::record_session_model_call(session_id, call);
  if (bus == nullptr) return;
  bus->publish<contract::StepLatency>({
      .session_id = session_id,
      .step = step,
      .streamed = streamed,
      .model_ms = call.model_ms,
      .ttft_ms = call.ttft_ms,
      .output_tokens = call.output_tokens,
      .reasoning_tokens = call.reasoning_tokens,
      .effort = call.effort,
      .ok = ok,
      .input_tokens = call.input_tokens,
      .cache_read_tokens = call.cache_read_tokens,
      .cache_write_tokens = call.cache_write_tokens,
      .variant = call.variant,
      .provider = call.provider,
      .model = call.model,
      .priced = call.cost.priced,
      .cost_input = call.cost.input,
      .cost_cache_read = call.cost.cache_read,
      .cost_cache_write = call.cost.cache_write,
      .cost_output = call.cost.output,
  });
}

}  // namespace qcode
