#pragma once

#include <map>
#include <string>
#include <vector>

namespace qcode {

// One reasoning variant from opencode.json: `"variants": {"<id>": {...}}`.
// Every field is optional; the id doubles as the wire effort by default.
struct VariantInfo {
    std::string id;           // picker / session value ("high", "ultra")
    std::string label;        // picker title (defaults to the id)
    std::string description;  // picker description
    std::string effort;       // wire effort (defaults to the id)
    int max_tokens = 0;       // request max_tokens for this variant (0 = model default)
    int budget_tokens = 0;    // thinking budget for budget-form models (0 = derived)
    std::string prompt;       // extra system instruction while the variant is active
};

// Catalog entry from opencode.json (providers.models).
struct ModelInfo {
    std::string name;
    std::string id;
    int context_window = 0;   // limit.context tokens; 0 => unknown (never guessed)
    double input_cost = 0.0;   // USD per 1M input tokens
    double output_cost = 0.0;  // USD per 1M output tokens
    bool reasoning = false;
    bool tool_call = false;
    bool vision = false;
    int output_limit = 0;
    // From opencode.json "max_tokens": the default request size (output
    // tokens), capped by output_limit. 0 = not configured.
    int max_tokens = 0;
    std::string protocol;
    // From opencode.json: reasoning_efforts / variants.
    std::vector<std::string> reasoning_efforts;
    // From opencode.json: reasoning_default / variant.
    std::string reasoning_default;
    // From opencode.json: reasoning_field (e.g. "reasoning").
    std::string reasoning_field;
    // From opencode.json "variants" object (ordered). Its ids are mirrored
    // into reasoning_efforts so every effort-list consumer sees them.
    std::vector<VariantInfo> variants;
    // From opencode.json "thinking": wire form for Claude (messages) routes.
    // type: "adaptive" (effort-driven) | "enabled" (budget_tokens) | "" (enabled).
    std::string thinking_type;
    // display: "summarized" | "omitted" | "" (adaptive defaults to summarized).
    std::string thinking_display;
    // allow_off=false: the API rejects disabled thinking, so /variant hides Off.
    bool thinking_allow_off = true;
    // From opencode.json cost.cache_read / cost.cache_write (USD per 1M tokens).
    double cache_read_cost = 0.0;
    double cache_write_cost = 0.0;
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
