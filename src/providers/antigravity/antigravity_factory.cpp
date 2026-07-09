#include "antigravity_factory.h"

#include "antigravity_client.h"

#include <memory>

namespace ai {
namespace antigravity {

namespace {
constexpr const char* kDefaultBaseUrl =
    "https://daily-cloudcode-pa.googleapis.com/v1internal";
}  // namespace

Client create_client() {
  return Client(
      std::make_unique<AntigravityClient>("unused", kDefaultBaseUrl));
}

Client create_client(const std::string& api_key) {
  return Client(std::make_unique<AntigravityClient>(api_key, kDefaultBaseUrl));
}

Client create_client(const std::string& api_key,
                     const std::string& base_url) {
  return Client(std::make_unique<AntigravityClient>(api_key, base_url));
}

}  // namespace antigravity
}  // namespace ai
