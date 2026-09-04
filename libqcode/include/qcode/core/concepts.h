#pragma once

#include <concepts>
#include <string_view>

namespace qcode {

/// Anything that can be viewed as a UTF-8 string without copying.
template <typename T>
concept StringViewable = std::convertible_to<T, std::string_view>;

}  // namespace qcode
