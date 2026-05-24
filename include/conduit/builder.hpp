#pragma once

/// @file
/// @brief Fluent builder for `EventEnvelope` plus the `conduit::event(...)`
///        and `conduit::make_event<T>(...)` entry points.

#include <conduit/envelope.hpp>
#include <conduit/event.hpp>
#include <conduit/flags.hpp>
#include <conduit/metadata.hpp>

#include <ulid/ulid.h>

#include <chrono>
#include <concepts>
#include <memory>
#include <string>
#include <utility>

#include <parcel/parcel.h>

namespace conduit {

template <typename T>
class EventBuilder {
    static_assert(std::derived_from<T, parcel::ICell>,
                  "EventBuilder<T>: T must derive from conduit::Event<T, Name>");

public:
    explicit EventBuilder(T payload) : payload_(std::move(payload)) {
        core_.flags = detail::collect_default_flags<T>();
    }

    [[nodiscard]] EventBuilder& id(const ulid::Ulid value) {
        core_.id = value;
        id_set_ = true;
        return *this;
    }

    [[nodiscard]] EventBuilder& correlation_id(ulid::Ulid value) {
        core_.correlation_id = value;
        return *this;
    }
    [[nodiscard]] EventBuilder& causation_id(ulid::Ulid value) {
        core_.causation_id = value;
        return *this;
    }

    [[nodiscard]] EventBuilder& metadata(std::string key, md::Value value) {
        core_.metadata.insert_or_assign(std::move(key), std::move(value));
        return *this;
    }
    [[nodiscard]] EventBuilder& metadata(Metadata md) {
        core_.metadata = std::move(md);
        return *this;
    }

    [[nodiscard]] EventBuilder& created_at(const std::chrono::system_clock::time_point tp) {
        core_.timestamps.created_at = tp;
        created_at_set_ = true;
        return *this;
    }

    template <typename F>
    [[nodiscard]] EventBuilder& flag() {
        core_.flags.set<F>();
        return *this;
    }

    template <typename... Fs>
    [[nodiscard]] EventBuilder& flags() {
        (core_.flags.set<Fs>(), ...);
        return *this;
    }

    [[nodiscard]] EventEnvelope build() {
        if (!id_set_) {
            core_.id = ulid::generate();
        }
        if (!created_at_set_) {
            core_.timestamps.created_at = std::chrono::system_clock::now();
        }
        auto payload_cell = std::make_shared<T>(std::move(payload_));
        return EventEnvelope(std::move(core_), parcel::cell_t{std::move(payload_cell)});
    }

    operator EventEnvelope() {
        return build();
    }  // NOLINT(google-explicit-constructor,hicpp-explicit-conversions)

private:
    detail::EnvelopeCore core_{};
    T payload_;
    bool id_set_ = false;
    bool created_at_set_ = false;
};

template <typename T>
[[nodiscard]] inline EventBuilder<T> event(T payload) {
    return EventBuilder<T>(std::move(payload));
}

template <typename T, typename... Args>
[[nodiscard]] inline EventBuilder<T> make_event(Args&&... args) {
    return EventBuilder<T>(T(std::forward<Args>(args)...));
}

}  // namespace conduit
