#pragma once

/// @file
/// @brief Transport interface and the local/remote scope enum used for flag-based filtering.

#include <conduit/envelope.hpp>

#include <cstdint>
#include <functional>
#include <utility>

namespace conduit {

class Bus;

/// Distinguishes in-process transports from off-machine ones. The Bus uses
/// this together with `flags::LocalOnly` / `flags::RemoteOnly` to decide
/// whether an envelope is dispatched through a given transport.
enum class TransportScope : std::uint8_t { Local, Remote };

/// Callable installed on a Transport at attach time that receives inbound
/// envelopes the transport pulled off the wire. The default sink built by
/// `Transport::attach(Bus&)` forwards directly to `Bus::deliver_to_listeners`.
/// Wrappers like `FilteredTransport` install a sink that interposes on the
/// inbound leg before forwarding to the bus.
using InboundSink = std::function<void(const EventEnvelopeView&)>;

class Transport {
public:
    Transport() = default;
    Transport(const Transport&) = delete;
    Transport(Transport&&) noexcept = default;
    Transport& operator=(const Transport&) = delete;
    Transport& operator=(Transport&&) noexcept = default;
    virtual ~Transport() = default;

    [[nodiscard]] virtual TransportScope scope() const noexcept = 0;

    /// Attach to a bus. The base implementation builds an inbound sink that
    /// forwards to `bus.deliver_to_listeners` and delegates to
    /// `attach_with_sink`. Subclasses that need to do per-attach work (open
    /// connections, subscribe to topics) should override `attach_with_sink`.
    virtual void attach(Bus& bus);

    /// Attach to a bus using a caller-supplied inbound sink. Wrappers use
    /// this to intercept the inbound leg without each transport needing to
    /// re-implement the hook.
    virtual void attach_with_sink(Bus& bus, InboundSink sink) {
        bus_ = &bus;
        inbound_sink_ = std::move(sink);
    }

    virtual void detach() noexcept {}
    virtual void dispatch(const EventEnvelopeView&) = 0;
    virtual void flush() {}

protected:
    /// Subclasses call this for inbound delivery instead of touching the bus
    /// directly. The sink installed at attach time decides what happens next.
    void deliver_inbound(const EventEnvelopeView& v) const {
        if (inbound_sink_) {
            inbound_sink_(v);
        }
    }

    [[nodiscard]] Bus* bus() const noexcept {
        return bus_;
    }

private:
    Bus* bus_ = nullptr;
    InboundSink inbound_sink_;
};

}  // namespace conduit
