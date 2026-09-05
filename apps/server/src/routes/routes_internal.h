#pragma once

#include "server_routes.h"
#include <httplib.h>
#include <memory>
#include <vector>

namespace qcode {
namespace server {

void register_system_routes(
    httplib::Server& svr,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers,
    const ServerSetupOptions& options);

void register_session_routes(
    httplib::Server& svr,
    std::shared_ptr<qcode::bus::BusRuntime> bus,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers,
    const ServerSetupOptions& options);

void register_terminal_routes(
    httplib::Server& svr);

void register_fs_routes(
    httplib::Server& svr);

void register_study_routes(
    httplib::Server& svr,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers);

}  // namespace server
}  // namespace qcode
