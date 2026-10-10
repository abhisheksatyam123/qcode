#include <qcode/generation/turn_prefix.h>

#include <qcode/core/logger.h>
#include <qcode/generation/generation_continue.h>
#include <qcode/tools/tool_catalog.h>
#include <qcode/transform/provider_transform.h>

#include <algorithm>

namespace qcode {

std::string build_turn_system_prompt(
    const std::string& system_prompt,
    bool is_subagent,
    const std::vector<ProviderInfo>& providers,
    std::string_view current_provider_id,
    std::string_view current_model_id) {
  std::string out = system_prompt;
  if (is_subagent) return out;
  out += std::string(kOrchestratorReminder);
  const std::string catalog_section = format_provider_catalog_for_prompt(
      providers, current_provider_id, current_model_id);
  if (!catalog_section.empty()) {
    out += "\n\n" + catalog_section;
  }
  return out;
}

ToolSet build_turn_tools(bool enable_task_tool, bool vision_supported) {
  const auto cfg = enable_task_tool ? ToolConfig::orchestrator(vision_supported)
                                    : ToolConfig::subagent(vision_supported);
  return ToolCatalog::build_definitions(cfg);
}

void apply_turn_sampling(GenerateOptions& opts, const ModelInfo* model,
                         const Model& transform_model) {
  // opencode.json sampling first; the family defaults only fill the gaps.
  if (!opts.temperature.has_value() &&
      (model == nullptr || model->temperature_supported)) {
    opts.temperature = model != nullptr && model->temperature.has_value()
                           ? model->temperature
                           : ProviderTransform::temperature(transform_model);
  }
  if (!opts.top_p.has_value()) {
    opts.top_p = model != nullptr && model->top_p.has_value()
                     ? model->top_p
                     : ProviderTransform::top_p(transform_model);
  }
  if (model != nullptr && !opts.max_tokens.has_value()) {
    // opencode.json max_tokens (model_defaults) capped by limit.output.
    opts.max_tokens = ProviderTransform::max_output_tokens(*model);
  }
}

void apply_variant_options(GenerateOptions& opts, const ModelInfo* model,
                           const std::string& requested,
                           const std::string& model_id) {
  std::string variant_id;
  if (model) {
    if (!requested.empty() || model->reasoning) {
      variant_id = ProviderTransform::resolve_session_variant(*model, requested);
    }
    if (!model->thinking_type.empty()) opts.thinking_type = model->thinking_type;
    if (!model->thinking_display.empty()) opts.thinking_display = model->thinking_display;
    // A reasoning model that returns no thinking text is indistinguishable
    // from one that never thought. The Responses API only emits readable
    // reasoning when a summary is requested, so ask for one unless the
    // config says otherwise ("none" opts out).
    if (model->reasoning) {
      opts.reasoning_summary = model->reasoning_summary.empty()
                                   ? std::string("auto")
                                   : model->reasoning_summary;
    }
  } else if (requested != "off") {
    variant_id = requested;
  }
  if (variant_id.empty() || variant_id == "off") return;

  const VariantInfo* variant =
      model ? ProviderTransform::find_variant(*model, variant_id) : nullptr;
  opts.reasoning_effort =
      model ? ProviderTransform::variant_wire_effort(*model, variant_id) : variant_id;
  opts.reasoning_variant = variant_id;
  if (variant != nullptr) {
    if (variant->budget_tokens > 0) opts.budget_tokens = variant->budget_tokens;
    if (variant->max_tokens > 0) {
      // limit.output is the hard cap; the variant only raises the request.
      const int cap = model->output_limit > 0 ? model->output_limit : variant->max_tokens;
      opts.max_tokens =
          std::max(opts.max_tokens.value_or(0), std::min(variant->max_tokens, cap));
    }
    if (!variant->prompt.empty()) {
      opts.system += "\n\n";
      opts.system += variant->prompt;
    }
  }
  if (!requested.empty() && variant_id != requested) {
    LOG_INFO("generation_service: clamped variant '{}' -> '{}' for {}", requested,
             variant_id, model_id);
  } else if (requested.empty()) {
    LOG_INFO("generation_service: default thinking variant '{}' for {}", variant_id,
             model_id);
  }
  LOG_INFO("generation_service: variant={} effort={} max_tokens={} budget_tokens={} "
           "prompt={} thinking={} model={}",
           variant_id, opts.reasoning_effort.value_or(""), opts.max_tokens.value_or(0),
           opts.budget_tokens.value_or(0), variant != nullptr && !variant->prompt.empty(),
           opts.thinking_type.value_or("default"), model_id);
}

Messages prepare_turn_history(Messages history, bool drop_system_notes) {
  history = apply_compaction_cutoff(std::move(history));
  if (drop_system_notes) {
    std::erase_if(history, [](const Message& message) {
      return message.role == kMessageRoleSystem;
    });
  }
  return history;
}

}  // namespace qcode
