#pragma once
#include <string>

namespace qcode {
namespace utils {

/** Generate a simple UUID-like string (timestamp + random hex). */
std::string generate_uuid();

} // namespace utils
} // namespace qcode
