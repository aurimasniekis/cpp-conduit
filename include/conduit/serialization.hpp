#pragma once

/// @file
/// @brief Thin `EventRegistry` wrapper around `parcel::ParcelRegistry`, plus
///        envelope encode/decode helpers for JSON and CBOR.

#include <conduit/envelope.hpp>
#include <conduit/event.hpp>
#include <conduit/parcel_cells.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <vector>

#include <parcel/parcel.h>

namespace conduit {

/// Raised by envelope/cell deserialization when the wire data is malformed.
/// `parcel::ParcelException`-derived errors are mapped here at the registry
/// boundary so callers can catch a single type.
class SerializationError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Registers event cells for wire decoding. Wraps a `parcel::ParcelRegistry`,
/// pre-registers the envelope cell and conduit's auxiliary cells, and exposes
/// thin `decode_json` / `decode_cbor` helpers that always return an
/// `EventEnvelope`.
///
/// `add_descriptor` is safe to call concurrently with `decode_json` /
/// `decode_cbor`: a writer builds a fresh inner registry from the current
/// snapshot under a unique lock, then swaps the active `shared_ptr` so
/// readers (which take a shared lock just long enough to copy the handle)
/// always observe a self-consistent state.
class EventRegistry {
public:
    EventRegistry() {
        auto reg = std::make_shared<parcel::ParcelRegistry>();
        reg->register_cells<EventEnvelope, UlidCell>();
        registry_ = std::move(reg);
    }

    EventRegistry(const EventRegistry& other) {
        const auto snap = other.snapshot();
        registry_ = std::make_shared<parcel::ParcelRegistry>(*snap);
    }

    EventRegistry(EventRegistry&&) noexcept = delete;
    EventRegistry& operator=(const EventRegistry&) = delete;
    EventRegistry& operator=(EventRegistry&&) noexcept = delete;
    ~EventRegistry() = default;

    /// Register the descriptor for an event type `T` (i.e. a class derived
    /// from `conduit::Event<T, Name>`).
    template <typename T>
    EventRegistry& add() {
        add_descriptor(T::descriptor());
        return *this;
    }

    /// Register a previously-resolved cell descriptor. Safe to call from any
    /// thread; concurrent readers see a self-consistent registry throughout
    /// the swap.
    EventRegistry& add_descriptor(parcel::cell_type_descriptor_t desc) {
        const auto current = snapshot();
        auto next = std::make_shared<parcel::ParcelRegistry>(*current);
        next->register_kind(std::move(desc));
        std::unique_lock lock(mu_);
        registry_ = std::move(next);
        return *this;
    }

    [[nodiscard]] EventEnvelope decode_json(parcel::json_t const& j) const {
        const auto reg = snapshot();
        try {
            const auto cell = EventEnvelope::from_json(j, *reg);
            const auto env = parcel::cell_cast<EventEnvelope>(cell);
            return *env;
        } catch (const parcel::ParcelException& e) {
            throw SerializationError{e.what()};
        }
    }

    [[nodiscard]] EventEnvelope decode_cbor(std::span<const std::uint8_t> bytes) const {
        try {
            const auto j = parcel::json_t::from_cbor(bytes);
            return decode_json(j);
        } catch (const parcel::ParcelException& e) {
            throw SerializationError{e.what()};
        } catch (const SerializationError&) {
            throw;
        } catch (const std::exception& e) {
            throw SerializationError{e.what()};
        }
    }

    [[nodiscard]] EventEnvelope decode_cbor(std::span<const char> bytes) const {
        try {
            const auto j = parcel::json_t::from_cbor(bytes);
            return decode_json(j);
        } catch (const parcel::ParcelException& e) {
            throw SerializationError{e.what()};
        } catch (const SerializationError&) {
            throw;
        } catch (const std::exception& e) {
            throw SerializationError{e.what()};
        }
    }

    /// Snapshot accessor for the underlying parcel registry. The returned
    /// shared_ptr pins one self-consistent registry version for its lifetime,
    /// even while `add_descriptor` concurrently swaps in newer versions.
    [[nodiscard]] std::shared_ptr<const parcel::ParcelRegistry>
    parcel_registry_snapshot() const noexcept {
        return snapshot();
    }

private:
    [[nodiscard]] std::shared_ptr<parcel::ParcelRegistry> snapshot() const {
        std::shared_lock lock(mu_);
        return registry_;
    }

    mutable std::shared_mutex mu_;
    std::shared_ptr<parcel::ParcelRegistry> registry_;
};

[[nodiscard]] inline parcel::json_t encode_json(const EventEnvelope& env) {
    return env.to_json();
}

[[nodiscard]] inline std::vector<char> encode_cbor(const EventEnvelope& env) {
    std::vector<char> out;
    parcel::json_t::to_cbor(env.to_json(), out);
    return out;
}

namespace serialization {

/// Source-compatibility alias for the prior `conduit::serialization::EventRegistry`
/// type. New code should use `conduit::EventRegistry` directly.
using EventRegistry = ::conduit::EventRegistry;

using ::conduit::encode_cbor;
using ::conduit::encode_json;

}  // namespace serialization

}  // namespace conduit
