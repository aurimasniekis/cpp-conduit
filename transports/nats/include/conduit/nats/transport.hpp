#pragma once

/// @file
/// @brief NATS transport adapter — one instance = one NATS subject in both directions.

#include <conduit/bus.hpp>
#include <conduit/envelope.hpp>
#include <conduit/exception.hpp>
#include <conduit/listener.hpp>
#include <conduit/serialization.hpp>
#include <conduit/transport.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace conduit::nats {

/// Operational/runtime failure inside the NATS transport adapter.
class NatsError : public conduit::TransportError {
public:
    using conduit::TransportError::TransportError;
};

/// Wire format used for encoded envelopes.
enum class Format : std::uint8_t { Json, Cbor };

struct TlsConfig {
    std::string ca_file;
    std::optional<std::string> cert_file;
    std::optional<std::string> key_file;
    bool verify_peer = true;
};

struct Config {
    /// NATS connection URL(s). Comma-separated for clusters.
    std::string url = "nats://localhost:4222";

    /// Optional connection name (empty -> ULID-derived).
    std::string name;

    std::optional<std::string> user;
    std::optional<std::string> password;
    std::optional<std::string> token;

    /// Path to a JWT / NKey credentials file (e.g. NATS NGS).
    std::optional<std::string> credentials_file;

    std::optional<TlsConfig> tls;

    std::chrono::seconds connect_timeout{10};
    std::chrono::seconds reconnect_wait{2};
    int max_reconnects = 60;

    /// Subject this transport binds to. Outbound `dispatch()` publishes onto
    /// this subject; the transport subscribes to the same subject at attach.
    /// Required — must be a non-empty single subject. Multi-subject routing is
    /// done by attaching multiple `Transport` instances (one per subject),
    /// optionally wrapped in `FilteredTransport` for event-name gating.
    std::string subject = "conduit.envelope";

    /// When set, the transport uses `natsConnection_QueueSubscribe` so that
    /// multiple subscribers form a load-balanced queue group.
    std::optional<std::string> queue_group;

    /// Wire format used for both outbound publishes and inbound decoding.
    Format format = Format::Cbor;
};

/// NATS pipe — one instance binds to a single NATS subject and carries traffic
/// in both directions: outbound `dispatch()` publishes on the subject, and any
/// inbound message on the subject is decoded via the shared event registry
/// and delivered through the inbound sink installed at attach time.
class Transport : public conduit::Transport {
public:
    /// Construct a NATS pipe. By default decoding uses the bus's registry —
    /// pass an explicit registry to scope a transport to a restricted set of
    /// event types (e.g. an audit pipe that only deserializes audit cells).
    explicit Transport(Config config, std::shared_ptr<EventRegistry> registry = {});

    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
    Transport(Transport&&) = delete;
    Transport& operator=(Transport&&) = delete;

    ~Transport() override;

    [[nodiscard]] TransportScope scope() const noexcept override {
        return TransportScope::Remote;
    }

    void attach_with_sink(Bus& bus, InboundSink sink) override;
    void detach() noexcept override;

    void dispatch(const EventEnvelopeView& v) override;
    void flush() override;

    [[nodiscard]] bool is_connected() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace conduit::nats
