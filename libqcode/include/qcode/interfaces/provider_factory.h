#pragma once

#include "abstract.h"
#include "antigravity_provider.h"
#include "openai_provider.h"
#include "anthropic_provider.h"
#include "cursor_provider.h"

#include <memory>
#include <string>

namespace qcode {

// Simple factory that creates concrete IProvider implementations based on a name.
// Supports "antigravity", "openai", "anthropic", and "cursor".
class ProviderFactory {
public:
    static std::unique_ptr<IProvider> create(const std::string& name,
                                             const std::string& api_key,
                                             const std::string& base_url_or_extra = "") {
        if (name == "antigravity") {
            return std::make_unique<AntigravityProviderAdapter>(api_key, base_url_or_extra);
        }
        if (name == "openai") {
            return std::make_unique<OpenAIProviderAdapter>(api_key, base_url_or_extra);
        }
        if (name == "anthropic") {
            return std::make_unique<AnthropicProviderAdapter>(api_key, base_url_or_extra);
        }
        if (name == "cursor") {
            return std::make_unique<CursorProviderAdapter>(api_key, base_url_or_extra);
        }
        return nullptr; // Unknown provider.
    }
};

} // namespace qcode
