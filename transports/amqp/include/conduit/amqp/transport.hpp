#pragma once

/// @file
/// @brief AMQP 0.9.1 (RabbitMQ et al.) transport adapter — one instance =
///        one routing key in both directions.

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

namespace conduit::amqp {

/// Wire format used for encoded envelopes.
enum class Format : std::uint8_t { Json, Cbor };

struct TlsConfig {
    std::string ca_file;
    std::optional<std::string> cert_file;
    std::optional<std::string> key_file;
    bool verify_peer = true;
};

struct Config {
    std::string url;               // "amqp://user:pass@host:5672/vhost" or "amqps://..."
    std::string connection_name;   // empty -> "conduit-<ulid>"
    std::optional<TlsConfig> tls;  // populated when scheme is amqps://

    // Topology (defaults: topic exchange + auto queue bound on routing_key).
    std::string exchange = "conduit";
    std::string exchange_type = "topic";  // "topic" | "direct" | "fanout"
    bool exchange_durable = true;
    bool exchange_auto_delete = false;

    /// Outbound publish routing key AND inbound binding pattern. For topic
    /// exchanges this can include wildcards (`*`, `#`).
    std::string routing_key = "conduit.envelope";
    std::string queue;  // empty -> server-generated, exclusive, auto-delete
    bool queue_durable = false;
    bool queue_exclusive = true;
    bool queue_auto_delete = true;

    bool persistent = false;          // delivery_mode=2 on publish if true
    bool publisher_confirms = false;  // enable confirm.select; required for RequireAck

    std::chrono::seconds connect_timeout{10};
    std::chrono::seconds heartbeat{30};

    /// Wire format used for both outbound publishes and inbound decoding.
    Format format = Format::Cbor;
};

/// AMQP pipe — one instance binds to a single AMQP routing key and carries
/// traffic in both directions: outbound `dispatch()` calls publish onto the
/// configured exchange with the configured routing key; inbound messages
/// arriving on the bound queue are decoded via the shared event registry
/// and delivered through the inbound sink installed at attach time.
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

}  // namespace conduit::amqp
