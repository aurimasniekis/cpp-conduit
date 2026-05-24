#pragma once

/// @file
/// @brief MQTT transport adapter — one instance = one MQTT topic in both directions.

#include <conduit/bus.hpp>
#include <conduit/envelope.hpp>
#include <conduit/listener.hpp>
#include <conduit/serialization.hpp>
#include <conduit/transport.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace conduit::mqtt {

/// Wire format used for encoded envelopes.
enum class Format : std::uint8_t { Json, Cbor };

struct TlsConfig {
    std::string ca_file;
    std::optional<std::string> cert_file;
    std::optional<std::string> key_file;
    bool verify_peer = true;
};

struct Config {
    std::string url;        // "tcp://host:1883" or "ssl://host:8883"
    std::string client_id;  // empty -> generated from ULID
    std::optional<std::string> username;
    std::optional<std::string> password;
    std::optional<TlsConfig> tls;
    int qos = 1;  // 0/1/2
    bool clean_session = true;
    std::chrono::seconds keep_alive{60};
    std::chrono::seconds connect_timeout{10};

    /// Topic this transport binds to. The transport publishes every envelope
    /// it receives onto this topic and subscribes to it at attach time.
    /// Required — must be a non-empty single topic; multi-topic routing is
    /// done by attaching multiple `Transport` instances to the bus (one per
    /// topic), gated by `FilteredTransport` if you need event-name filtering.
    std::string topic = "conduit/envelope";

    /// Wire format used for both outbound publishes and inbound decoding.
    Format format = Format::Cbor;
};

/// MQTT pipe — one instance binds to a single MQTT topic and carries traffic
/// in both directions: outbound `dispatch()` calls publish onto the topic,
/// and any inbound message on the topic is decoded via the shared event
/// registry and delivered through the inbound sink installed at attach time.
class Transport : public conduit::Transport {
public:
    /// Construct an MQTT pipe. By default, decoding uses the bus's registry —
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

    // Overrides `attach_with_sink`; the base `attach(Bus&)` default builds a
    // bus-default sink and delegates here, so wrapping with FilteredTransport
    // (which calls `attach_with_sink` directly) works without any extra glue.
    void attach_with_sink(Bus& bus, InboundSink sink) override;
    void detach() noexcept override;

    void dispatch(const EventEnvelopeView& v) override;
    void flush() override;

    [[nodiscard]] bool is_connected() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace conduit::mqtt
