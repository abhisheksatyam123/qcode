#pragma once
#include <string>

namespace ai {
namespace utils {

/** Generate a simple UUID-like string (timestamp + random hex). */
std::string generate_uuid();

} // namespace utils
} // namespace ai
