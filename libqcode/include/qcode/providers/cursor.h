#pragma once

#ifndef QCODE_HAS_CURSOR
#error \
    "Cursor component not available. Link with qcode::cursor or qcode::sdk to use Cursor functionality."
#endif

#include <qcode/types/client.h>

#include <string>
#include <vector>

namespace qcode {
namespace cursor {

// Cursor service endpoints. Both authenticate with the same access token; the
// resolver in the TUI fills these in from defaults (or the config `api` field).
struct Options {
  std::string aiserver_base_url = "https://api2.cursor.sh";
  std::string agent_base_url = "https://agentn.global.api5.cursor.sh";
};

struct AvailableModel {
  std::string id;
  std::string name;
};

// Create a Cursor client with an explicit access token.
// @param access_token Cursor access token (from get_cursor_access_token()).
Client create_client(const std::string& access_token);

// Create a Cursor client with an explicit access token and session token.
Client create_client(const std::string& access_token,
                     const std::string& session_token);

// Create a Cursor client overriding the default service base URLs.
Client create_client(const std::string& access_token,
                     const std::string& session_token,
                     const std::string& aiserver_base_url,
                     const std::string& agent_base_url);

// Create a Cursor client from an Options struct.
Client create_client(const std::string& access_token, const Options& options);

// Load the models enabled for this Cursor account.
std::vector<AvailableModel> list_models(const std::string& access_token,
                                        const Options& options = {});

}  // namespace cursor
}  // namespace qcode
