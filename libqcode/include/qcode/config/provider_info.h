#pragma once

#include <map>
#include <string>
#include <vector>

namespace qcode {

// Catalog entry from opencode.json (providers.models).
struct ModelInfo {
    std::string name;
    std::string id;
    int context_window = 0;   // tokens; 0 => unknown (fallback)
    double input_cost = 0.0;   // USD per 1M input tokens
    double output_cost = 0.0;  // USD per 1M output tokens
    bool reasoning = false;
    bool tool_call = false;
    bool vision = false;
    int output_limit = 0;
    std::string protocol;
    // From opencode.json: reasoning_efforts / variants.
    std::vector<std::string> reasoning_efforts;
    // From opencode.json: reasoning_default / variant.
    std::string reasoning_default;
    // From opencode.json: reasoning_field (e.g. "reasoning").
    std::string reasoning_field;
};

struct ProviderInfo {
    std::string name;
    std::string id;
    std::string api_url;
    std::string api_key;
    std::map<std::string, std::string> headers;
    std::string protocol = "chat_completions";
    std::string project_id;
    std::vector<ModelInfo> models;
};

// Check if a provider has valid credentials / endpoint to execute calls.
bool is_provider_authenticated(const ProviderInfo& provider);

// Check if a model is currently eligible and working for delegation.
bool is_model_working(const ProviderInfo& provider, const ModelInfo& model);

// Filter providers and their models to only those that are working.
std::vector<ProviderInfo> filter_working_providers(const std::vector<ProviderInfo>& providers);

// Format a clean Markdown section of available providers and models for the system prompt.
std::string format_provider_catalog_for_prompt(
    const std::vector<ProviderInfo>& providers,
    std::string_view current_provider_id = "",
    std::string_view current_model_id = "");

// Compact `provider:model_id` list for task-tool error payloads (from opencode.json).
std::string format_provider_catalog_for_error(
    const std::vector<ProviderInfo>& providers,
    std::string_view current_provider_id = "",
    std::string_view current_model_id = "");

}  // namespace qcode
