#pragma once

/// @file
/// @brief Type-based flag set, built-in flag tags, and helpers.
///
/// Each flag type derives from `Flag<"some.name">`. The fixed-string name is
/// the flag's stable identity — across processes, across translation units,
/// and on the wire — so serialization just lists the names directly.

#include <conduit/fixed_string.hpp>

#include <cstddef>
#include <functional>
#include <set>
#include <string>
#include <string_view>

namespace conduit::flags {

/// Base for all flag tag types. Users extend by writing
/// `struct MyFlag : conduit::flags::Flag<"my.flag"> {};`.
template <FixedString Name>
struct Flag {
    static constexpr std::string_view name_v = Name.view();
};

// ---------------------------------------------------------------------------
// Built-in flag tag types.
// ---------------------------------------------------------------------------

/// Force same-thread dispatch even in Queue / ThreadPool execution modes.
struct Direct : Flag<"direct"> {};
/// Hint to durable transports that they should persist the envelope.
struct Durable : Flag<"durable"> {};
/// Hint to transports to use a persistent (human-readable) wire format.
struct Persistent : Flag<"persistent"> {};
/// Skip the middleware pipeline for this envelope.
struct NoMiddleware : Flag<"no_middleware"> {};
/// Request the broker / transport acknowledge the publish before returning.
struct RequireAck : Flag<"require_ack"> {};
/// Hint that this envelope is intended for fan-out broadcast.
struct Broadcast : Flag<"broadcast"> {};

/// Restrict dispatch to transports with `TransportScope::Local`.
struct LocalOnly : Flag<"local_only"> {};
/// Restrict dispatch to transports with `TransportScope::Remote`.
struct RemoteOnly : Flag<"remote_only"> {};

// ---------------------------------------------------------------------------
// FlagSet
// ---------------------------------------------------------------------------

/// Ordered set of flag names. Iteration order is deterministic (lexicographic
/// by name), so serialization output is stable.
class FlagSet {
public:
    FlagSet() = default;

    template <typename F>
    [[nodiscard]] bool has() const noexcept {
        return data_.contains(std::string_view{F::name_v});
    }

    template <typename F>
    FlagSet& set() {
        data_.emplace(F::name_v);
        return *this;
    }

    template <typename F>
    FlagSet& unset() {
        if (const auto it = data_.find(std::string_view{F::name_v}); it != data_.end()) {
            data_.erase(it);
        }
        return *this;
    }

    template <typename... Fs>
    [[nodiscard]] static FlagSet of() {
        FlagSet s;
        (s.set<Fs>(), ...);
        return s;
    }

    [[nodiscard]] bool empty() const noexcept {
        return data_.empty();
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return data_.size();
    }

    FlagSet& operator|=(const FlagSet& other) {
        data_.insert(other.data_.begin(), other.data_.end());
        return *this;
    }

    [[nodiscard]] friend FlagSet operator|(FlagSet lhs, const FlagSet& rhs) {
        lhs |= rhs;
        return lhs;
    }

    [[nodiscard]] bool operator==(const FlagSet& other) const noexcept {
        return data_ == other.data_;
    }

    [[nodiscard]] auto begin() const noexcept {
        return data_.begin();
    }
    [[nodiscard]] auto end() const noexcept {
        return data_.end();
    }

    [[nodiscard]] const std::set<std::string, std::less<>>& names() const noexcept {
        return data_;
    }

    /// Add a flag by its wire name — used by serialization when decoding.
    void set_by_name(std::string_view name) {
        data_.emplace(name);
    }

    [[nodiscard]] bool has_name(const std::string_view name) const noexcept {
        return data_.contains(name);
    }

private:
    std::set<std::string, std::less<>> data_;
};

}  // namespace conduit::flags
