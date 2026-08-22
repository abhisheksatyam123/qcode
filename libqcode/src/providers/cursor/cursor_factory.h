#pragma once

#include <qcode/providers/cursor.h>
#include <qcode/core/client.h>

#include <string>
#include <vector>

namespace qcode {
namespace cursor {

// Create a Cursor client.
// @param access_token Cursor access token (from qcode::get_cursor_access_token()).
Client create_client(const std::string& access_token);
Client create_client(const std::string& access_token, const Options& options);
Client create_client(const std::string& access_token,
                     const std::string& session_token);
Client create_client(const std::string& access_token,
                     const std::string& session_token,
                     const std::string& aiserver_base_url,
                     const std::string& agent_base_url);
std::vector<AvailableModel> list_models(const std::string& access_token,
                                        const Options& options);

}  // namespace cursor
}  // namespace qcode
