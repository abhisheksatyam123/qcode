#pragma once

#include <qcode/bus/in_process_bus.h>
#include <qcode/config/config.h>
#include <qcode/generation/generation_service.h>

#include <httplib.h>

#include <memory>
#include <string>
#include <vector>

namespace qcode {
namespace server {

struct ServerSetupOptions {
  std::string webui_dir;
  std::string default_workspace;
};

// Owns the GenerationService used by HTTP generation routes.
extern std::unique_ptr<qcode::GenerationService> g_backend;

void setup_server_routes(
    httplib::Server& server,
    std::shared_ptr<qcode::bus::BusRuntime> bus,
    std::shared_ptr<std::vector<qcode::ProviderInfo>> providers,
    const ServerSetupOptions& options);

}  // namespace server
}  // namespace qcode
