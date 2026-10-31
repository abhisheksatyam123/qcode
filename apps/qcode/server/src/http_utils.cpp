#include "http_utils.h"

#include <cstdlib>

namespace qcode {
namespace server {

std::string url_decode(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%') {
            if (i + 2 < value.size()) {
                char hex[3] = { value[i+1], value[i+2], '\0' };
                result += static_cast<char>(std::strtol(hex, nullptr, 16));
                i += 2;
            } else {
                result += '%';
            }
        } else if (value[i] == '+') {
            result += ' ';
        } else {
            result += value[i];
        }
    }
    return result;
}


}  // namespace server
}  // namespace qcode
