#pragma once

/// @file
/// @brief ZeroMQ transport adapter — supports PubSub, PushPull and RouterDealer.

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

namespace conduit::zmq {

/// Wire format used for encoded envelopes.
enum class Format : std::uint8_t { Json, Cbor };

/// ZMQ socket pattern. Each pattern picks a different socket-type pair and
/// expects different endpoint config to be populated.
enum class Pattern : std::uint8_t {
    /// PUB outbound, SUB inbound. Use for one-to-many fan-out broadcasts.
    PubSub,
    /// PUSH outbound, PULL inbound. Round-robin pipeline.
    PushPull,
    /// ROUTER+DEALER on a single bidirectional socket.
    RouterDealer,
};

/// Per-socket role. `Bind` listens on the endpoint; `Connect` dials it.
/// Conventional roles are pattern-specific (Pub binds, Sub connects; Pull
/// binds, Push connects), but ZMQ allows the opposite — keep both
/// configurable.
enum class Role : std::uint8_t { Bind, Connect };

/// CurveZMQ authentication parameters (gated by `CONDUIT_TRANSPORT_ZMQ_CURVE`).
struct CurveConfig {
    /// Z85-encoded 40-byte public key of the server.
    std::string server_key;
    /// Z85-encoded 40-byte public key of this socket.
    std::string public_key;
    /// Z85-encoded 40-byte secret key of this socket.
    std::string secret_key;
    /// When true, this socket is the Curve server; otherwise a client.
    bool is_server = false;
};

struct Config {
    Pattern pattern = Pattern::PubSub;

    // PubSub — pub_endpoint is required when pattern == PubSub.
    std::string pub_endpoint;
    Role pub_role = Role::Connect;
    std::string sub_endpoint;
    Role sub_role = Role::Connect;
    /// ZMQ subscription filter prefix; empty means "subscribe to everything".
    std::string subscription_prefix;

    // PushPull — both endpoints required when pattern == PushPull.
    std::string push_endpoint;
    Role push_role = Role::Connect;
    std::string pull_endpoint;
    Role pull_role = Role::Bind;

    // RouterDealer — single bidirectional socket.
    std::string endpoint;
    Role endpoint_role = Role::Connect;

    /// Optional socket identity / routing-id.
    std::optional<std::string> identity;

    std::chrono::milliseconds linger{0};
    int send_hwm = 1000;
    int recv_hwm = 1000;

    std::optional<CurveConfig> curve;

    Format format = Format::Cbor;
};

/// ZeroMQ pipe — one instance covers a pattern-specific pair of sockets and
/// carries traffic in both directions: outbound `dispatch()` sends on the
/// publish/push/router leg, and inbound traffic is decoded on a dedicated
/// receive thread and delivered through the inbound sink installed at attach
/// time.
class Transport : public conduit::Transport {
public:
    /// Construct a ZMQ pipe. The constructor validates pattern-specific
    /// endpoint config and throws `std::invalid_argument` if anything is
    /// missing.
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

}  // namespace conduit::zmq
