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

void register_terminal_routes(
    httplib::Server& svr);

void register_fs_routes(
    httplib::Server& svr);

void register_vision_routes(
    httplib::Server& svr,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers);

void register_study_routes(
    httplib::Server& svr,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers);

}  // namespace server
}  // namespace qcode
