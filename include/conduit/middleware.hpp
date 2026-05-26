#pragma once

/// @file
/// @brief Middleware interface for the bus dispatch pipeline.

#include <conduit/envelope.hpp>

#include <commons/prioritized.hpp>

#include <exception>
#include <string_view>

namespace conduit {

class Middleware : public comms::Prioritized {
public:
    Middleware() = default;
    Middleware(const Middleware&) = default;
    Middleware(Middleware&&) noexcept = default;
    Middleware& operator=(const Middleware&) = default;
    Middleware& operator=(Middleware&&) noexcept = default;
    ~Middleware() override = default;

    /// Called before listener dispatch. Return false to drop the envelope.
    virtual bool before_dispatch(EventEnvelopeView& /*v*/) {
        return true;
    }
    /// Called after all listeners ran (or were skipped if before_dispatch returned false).
    virtual void after_dispatch(EventEnvelopeView& /*v*/) {}
    /// Called when a listener throws.
    virtual void on_error(EventEnvelopeView& /*v*/, const std::exception_ptr& /*ep*/) {}

    /// Called when a transport fails to decode or otherwise produce an inbound
    /// envelope (no envelope available). `transport` is the adapter's short
    /// name ("mqtt", "nats", "zmq", "amqp", "redis", ...). Default is a no-op.
    virtual void on_transport_error(std::string_view /*transport*/,
                                    const std::exception_ptr& /*ep*/) {}
};

}  // namespace conduit
