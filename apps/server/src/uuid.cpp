#include <ai/utils/uuid.h>
#include <uuid.h>

namespace ai {
namespace utils {

std::string generate_uuid() {
    // RFC 4122 v4 UUID via the vendored stduuid library (thread-safe).
    static thread_local std::mt19937 engine{std::random_device{}()};
    static thread_local uuids::uuid_random_generator gen{engine};
    return uuids::to_string(gen());
}

} // namespace utils
} // namespace ai
