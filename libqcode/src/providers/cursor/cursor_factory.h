#pragma once

#include "ai/cursor.h"
#include "ai/types/client.h"

#include <string>
#include <vector>

namespace ai {
namespace cursor {

// Create a Cursor client.
// @param access_token Cursor access token (from ai::tui::get_cursor_access_token()).
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
}  // namespace ai
