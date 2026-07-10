#include "antigravity_factory.h"

#include "ai/antigravity.h"
#include "antigravity_client.h"

#include <memory>

namespace ai {
namespace antigravity {

namespace {
constexpr const char* kDefaultBaseUrl =
    "https://daily-cloudcode-pa.sandbox.googleapis.com/v1internal";
}  // namespace

Client create_client(const std::string& api_key) {
  return Client(std::make_unique<AntigravityClient>(api_key, kDefaultBaseUrl));
}

Client create_client(const std::string& api_key,
                     const std::string& base_url) {
  return Client(std::make_unique<AntigravityClient>(api_key, base_url));
}

Client create_client(const std::string& api_key, const Options& options) {
  return Client(std::make_unique<AntigravityClient>(
      api_key, options.base_url.empty() ? kDefaultBaseUrl : options.base_url,
      options.project_id));
}

}  // namespace antigravity
}  // namespace ai
