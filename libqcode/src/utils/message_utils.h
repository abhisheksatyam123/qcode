#pragma once

#include <qcode/core/message.h>

#include <string>

namespace qcode {
namespace utils {

// Convert MessageRole enum to string representation
inline std::string message_role_to_string(MessageRole role) {
  switch (role) {
    case kMessageRoleSystem:
      return "system";
    case kMessageRoleUser:
      return "user";
    case kMessageRoleAssistant:
      return "assistant";
    default:
      return "user";
  }
}

}  // namespace utils
}  // namespace qcode