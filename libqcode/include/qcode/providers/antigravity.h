#pragma once

#ifndef QCODE_HAS_ANTIGRAVITY
#error \
    "Antigravity component not available. Link qcode::engine (defines QCODE_HAS_ANTIGRAVITY)."
#endif

#include <qcode/core/retry_policy.h>
#include <qcode/core/client.h>

#include <optional>
#include <string>

namespace qcode {
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
}  // namespace qcode
