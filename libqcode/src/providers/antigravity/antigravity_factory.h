#pragma once

#include <qcode/types/client.h>

#include <string>

namespace ai {
namespace antigravity {

Client create_client(const std::string& api_key);
Client create_client(const std::string& api_key, const std::string& base_url);

}  // namespace antigravity
}  // namespace ai
