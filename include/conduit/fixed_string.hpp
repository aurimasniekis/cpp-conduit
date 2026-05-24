#pragma once

/// @file
/// @brief NTTP-friendly fixed-size string used as a non-type template parameter.

#include <cstddef>
#include <string_view>

namespace conduit {

/// Compile-time fixed-size string usable as a non-type template parameter.
///
/// Construct via CTAD from a string literal:
///
///     FixedString id{"order.created"};   // N = 14 (includes null terminator)
///
/// The class is a structural type so it can appear directly in template
/// argument lists, e.g. `Event<"order.created">`.
template <std::size_t N>
struct FixedString {
    // NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,google-explicit-constructor,hicpp-explicit-conversions)
    char value[N]{};

    /// Implicit by design: enables `Event<"foo">` literal syntax.
    constexpr FixedString(const char (&str)[N]) noexcept {
        for (std::size_t i = 0; i < N; ++i) {
            value[i] = str[i];
        }
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return std::string_view{static_cast<const char*>(value), N - 1};
    }

    [[nodiscard]] constexpr operator std::string_view() const noexcept {
        return view();
    }
    // NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,google-explicit-constructor,hicpp-explicit-conversions)

    [[nodiscard]] static constexpr std::size_t size() noexcept {
        return N - 1;
    }

    [[nodiscard]] static constexpr bool empty() noexcept {
        return N <= 1;
    }

    template <std::size_t M>
    [[nodiscard]] constexpr bool operator==(const FixedString<M>& other) const noexcept {
        if constexpr (M != N) {
            return false;
        } else {
            for (std::size_t i = 0; i < N; ++i) {
                if (value[i] != other.value[i]) {
                    return false;
                }
            }
            return true;
        }
    }
};

}  // namespace conduit
