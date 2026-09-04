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

}  // namespace qcode
