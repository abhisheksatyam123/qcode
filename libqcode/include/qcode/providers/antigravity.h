#pragma once

#ifndef AI_SDK_HAS_ANTIGRAVITY
#error \
    "Antigravity component not available. Link with ai::antigravity or ai::sdk to use Antigravity functionality."
#endif

#include "retry/retry_policy.h"
#include "types/client.h"

#include <optional>
#include <string>

namespace ai {
namespace antigravity {

struct Options {
  std::string base_url;
  std::string project_id;
};

/// Create an Antigravity client with an explicit API key (token).
/// @param api_key Antigravity bearer token (from get_antigravity_token()).
/// @return Configured Antigravity client.
Client create_client(const std::string& api_key);

/// Create an Antigravity client with explicit API key and base URL.
/// @param api_key Antigravity bearer token.
/// @param base_url Google Vertex endpoint (default daily-cloudcode-pa).
/// @return Configured Antigravity client.
Client create_client(const std::string& api_key, const std::string& base_url);

Client create_client(const std::string& api_key, const Options& options);

}  // namespace antigravity
}  // namespace ai
