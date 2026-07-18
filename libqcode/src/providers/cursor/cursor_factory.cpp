#include "cursor_factory.h"

#include <qcode/providers/cursor.h>
#include "cursor_client.h"

#include <memory>

namespace qcode {
namespace cursor {
namespace {
constexpr const char* kDefaultAiserver = "https://api2.cursor.sh";
constexpr const char* kDefaultAgent = "https://agentn.global.api5.cursor.sh";
}  // namespace

Client create_client(const std::string& access_token) {
  return Client(
      std::make_unique<CursorClient>(access_token, ""));
}

Client create_client(const std::string& access_token,
                     const std::string& session_token) {
  return Client(
      std::make_unique<CursorClient>(access_token, session_token));
}

Client create_client(const std::string& access_token,
                     const std::string& session_token,
                     const std::string& aiserver_base_url,
                     const std::string& agent_base_url) {
  return Client(std::make_unique<CursorClient>(
      access_token, session_token, aiserver_base_url, agent_base_url));
}
Client create_client(const std::string& access_token, const Options& options) {
  return Client(std::make_unique<CursorClient>(
      access_token, access_token, options.aiserver_base_url,
      options.agent_base_url));
}

std::vector<AvailableModel> list_models(const std::string& access_token,
                                        const Options& options) {
  CursorClient client(access_token, access_token, options.aiserver_base_url,
                      options.agent_base_url);
  std::vector<AvailableModel> models;
  for (auto&& model : client.available_models()) {
    models.emplace_back(
        AvailableModel{std::move(model.id), std::move(model.name)});
  }
  return models;
}

}  // namespace cursor
}  // namespace qcode
