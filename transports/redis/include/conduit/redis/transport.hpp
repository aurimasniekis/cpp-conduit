#pragma once

/// @file
/// @brief Redis pub/sub transport adapter — one instance = one Redis channel.

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

namespace conduit::redis {

/// Wire format used for encoded envelopes.
enum class Format : std::uint8_t { Json, Cbor };

struct TlsConfig {
    std::string ca_file;
    std::optional<std::string> cert_file;
    std::optional<std::string> key_file;
    bool verify_peer = true;
};

struct Config {
    std::string url;                      // "tcp://host:6379" or "tls://host:6380"
    std::optional<std::string> username;  // ACL user (Redis 6+)
    std::optional<std::string> password;
    std::optional<int> db;  // SELECT after AUTH; default 0
    std::optional<TlsConfig> tls;

    std::chrono::seconds connect_timeout{10};

    /// Channel this transport binds to (PUBLISH and SUBSCRIBE both target it).
    /// Required — must be non-empty. Multi-channel routing is done by
    /// attaching multiple `Transport` instances wrapped in
    /// `FilteredTransport`, matching MQTT's per-topic-instance model.
    std::string channel = "conduit:envelope";

    /// Wire format used for both outbound publishes and inbound decoding.
    Format format = Format::Cbor;
};

/// Redis pub/sub pipe — one instance binds to a single channel and carries
/// traffic in both directions: outbound `dispatch()` calls `PUBLISH` on the
/// channel, and any inbound message arrives on a dedicated subscriber thread,
/// is decoded via the shared event registry, and is delivered through the
/// inbound sink installed at attach time.
///
/// NOTE: Redis `PUBLISH` returns the number of subscribers that received the
/// message. When `flags::RequireAck` is set on an envelope, a return of 0 is
/// treated as a delivery miss (logged via the future middleware hook) but
/// does *not* throw — matching MQTT QoS 0/1's best-effort feel.
class Transport : public conduit::Transport {
public:
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

}  // namespace conduit::redis
