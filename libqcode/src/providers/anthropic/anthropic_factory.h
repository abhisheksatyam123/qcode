#pragma once

#include <qcode/core/client.h>
#include <qcode/providers/anthropic.h>

#include <string>

namespace qcode {
namespace anthropic {

Client create_client();
Client create_client(const std::string& api_key);
Client create_client(const std::string& api_key, const std::string& base_url);
Client create_client(const std::string& api_key,
                     const std::string& base_url,
                     const retry::RetryConfig& retry_config);
Client create_client(const std::string& api_key,
                     const CompatibleOptions& options);

}  // namespace anthropic
}  // namespace qcode