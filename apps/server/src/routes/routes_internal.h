#pragma once

#include "server_routes.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <vector>

namespace qcode {
namespace server {

std::string load_persona_prompt(const std::string& persona, const std::string& workspace);
nlohmann::json list_available_personas(const std::string& workspace);

// A provider/model resolved against the configured providers.
struct ResolvedModel {
    const qcode::ProviderInfo* provider = nullptr;
    const qcode::ModelInfo* model = nullptr;  // null when the provider lists no models
};

// Resolve `provider` / `model` by id or name. An empty provider means the
// first configured one; an empty model the provider's first model. A *named*
// model that is not in the provider's catalog yields a null model and a 400
// on `res` listing that provider's model ids. A provider that is named but
// not configured yields a null provider and a 400 on `res` listing the
// configured provider ids.
ResolvedModel resolve_provider_model(const std::vector<qcode::ProviderInfo>& providers,
                                     const std::string& provider,
                                     const std::string& model,
                                     httplib::Response& res);

void register_system_routes(
    httplib::Server& svr,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers,
    const ServerSetupOptions& options);

void register_session_routes(
    httplib::Server& svr,
    std::shared_ptr<qcode::bus::BusRuntime> bus,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers,
    const ServerSetupOptions& options);

void register_session_ops_routes(
    httplib::Server& svr,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers);

void register_fs_routes(
    httplib::Server& svr);

}  // namespace server
}  // namespace qcode
