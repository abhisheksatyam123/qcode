#include <qcode/utils/random.h>

#include <mutex>
#include <random>

namespace qcode {
namespace utils {
namespace {

std::mutex random_mutex;
std::mt19937 random_generator{std::random_device{}()};

int random_nibble() {
  static std::uniform_int_distribution<int> distribution{0, 15};
  return distribution(random_generator);
}

}  // namespace

std::string random_hex(std::size_t length) {
  constexpr char kHex[] = "0123456789abcdef";
  std::lock_guard lock(random_mutex);
  std::string result;
  result.reserve(length);
  for (std::size_t i = 0; i < length; ++i) {
    result += kHex[random_nibble()];
  }
  return result;
}

std::string new_uuid() {
  auto value = random_hex(32);
  value[12] = '4';
  constexpr char kVariants[] = "89ab";
  {
    std::lock_guard lock(random_mutex);
    value[16] = kVariants[random_nibble() % 4];
  }
  value.insert(20, "-");
  value.insert(16, "-");
  value.insert(12, "-");
  value.insert(8, "-");
  return value;
}

}  // namespace utils
}  // namespace qcode
