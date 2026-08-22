#pragma once

#include <cstddef>
#include <string>

namespace qcode {
namespace utils {

std::string new_uuid();
std::string random_hex(std::size_t length);

}  // namespace utils
}  // namespace qcode
