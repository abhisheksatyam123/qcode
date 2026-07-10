#include <ai/utils/uuid.h>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

namespace ai {
namespace utils {

std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::stringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(12) << ms
       << std::setw(8) << (dis(gen) & 0xFFFFFFFF)
       << std::setw(8) << (dis(gen) & 0xFFFFFFFF);
    return ss.str();
}

} // namespace utils
} // namespace ai
