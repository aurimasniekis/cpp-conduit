#pragma once

/// @file
/// @brief Event-name glob matcher.
///
/// Semantics:
///   - `*`  matches any sequence of characters except `.` within one segment.
///   - `**` matches any sequence including `.` (crosses segments).
///   - Anything else matches exactly.

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace conduit {

namespace detail {

constexpr bool glob_match_impl(const std::string_view pattern,
                               const std::string_view name) noexcept {
    const std::size_t plen = pattern.size();
    const std::size_t nlen = name.size();

    std::size_t pi = 0;
    std::size_t ni = 0;

    constexpr std::size_t k_no_star = std::numeric_limits<std::size_t>::max();
    std::size_t star_pi = k_no_star;
    std::size_t star_ni = 0;
    bool star_is_dd = false;  // true if the last unresolved star was '**'

    while (ni < nlen) {
        if (pi < plen && pattern[pi] == '*') {
            const bool is_double = (pi + 1 < plen) && pattern[pi + 1] == '*';
            star_pi = pi;
            star_is_dd = is_double;
            star_ni = ni;
            pi += is_double ? 2 : 1;
            continue;
        }

        if (pi < plen && pattern[pi] == name[ni]) {
            ++pi;
            ++ni;
            continue;
        }

        if (star_pi != k_no_star) {
            // Backtrack to last '*' (or '**'). Single '*' refuses to cross '.'.
            if (!star_is_dd && name[star_ni] == '.') {
                return false;
            }
            ++star_ni;
            if (!star_is_dd && star_ni <= nlen && star_ni > 0 && name[star_ni - 1] == '.') {
                return false;
            }
            ni = star_ni;
            pi = star_pi + (star_is_dd ? 2 : 1);
            continue;
        }

        return false;
    }

    // Consume trailing '*' / '**' in pattern.
    while (pi < plen && pattern[pi] == '*') {
        if (pi + 1 < plen && pattern[pi + 1] == '*') {
            pi += 2;
        } else {
            pi += 1;
        }
    }

    return pi == plen;
}

}  // namespace detail

/// Event-name glob matcher.
class Glob {
public:
    explicit Glob(const std::string_view pattern) : storage_(pattern) {}
    explicit Glob(const char* pattern) : storage_(pattern) {}
    explicit Glob(std::string pattern) noexcept : storage_(std::move(pattern)) {}

    Glob(const Glob&) = default;
    Glob(Glob&&) noexcept = default;
    Glob& operator=(const Glob&) = default;
    Glob& operator=(Glob&&) noexcept = default;
    ~Glob() = default;

    [[nodiscard]] bool matches(const std::string_view name) const noexcept {
        return detail::glob_match_impl(storage_, name);
    }

    [[nodiscard]] std::string_view pattern() const noexcept {
        return storage_;
    }

    /// Free function helper for one-shot matching.
    [[nodiscard]] static constexpr bool match(const std::string_view pattern,
                                              const std::string_view name) noexcept {
        return detail::glob_match_impl(pattern, name);
    }

private:
    std::string storage_;
};

}  // namespace conduit
