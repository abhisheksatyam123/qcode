#pragma once

#include <qcode/core/client.h>

#include <string>

namespace qcode {
namespace anthropic {

Client create_client();
Client create_client(const std::string& api_key);
Client create_client(const std::string& api_key, const std::string& base_url);

}  // namespace anthropic
}  // namespace qcode