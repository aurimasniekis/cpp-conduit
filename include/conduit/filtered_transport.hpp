#pragma once

/// @file
/// @brief `FilteredTransport` — wraps any transport with bidirectional
///        publish/deliver predicates.

#include <conduit/bus.hpp>
#include <conduit/envelope.hpp>
#include <conduit/transport.hpp>

#include <functional>
#include <memory>
#include <utility>

namespace conduit {

/// Wrapper that gates events flowing through an inner transport on both legs.
/// The `outbound` predicate runs in `dispatch()` — false suppresses the
/// publish. The `inbound` predicate runs on the sink installed at attach time
/// — false suppresses delivery to the bus's listeners. Either predicate may
/// be empty, meaning "pass everything." A predicate that throws is treated
/// the same as one that returned false; the envelope is dropped.
///
/// `scope()`, `detach()`, and `flush()` simply forward to the inner transport
/// — wrappers don't shift the local/remote scope or own broker connections.
class FilteredTransport : public Transport {
public:
    using Predicate = std::function<bool(const EventEnvelopeView&)>;

    explicit FilteredTransport(std::shared_ptr<Transport> inner,
                               Predicate outbound = {},
                               Predicate inbound = {})
        : inner_(std::move(inner)), outbound_(std::move(outbound)), inbound_(std::move(inbound)) {}

    [[nodiscard]] TransportScope scope() const noexcept override {
        return inner_->scope();
    }

    void attach(Bus& bus) override {
        Predicate p = inbound_;
        inner_->attach_with_sink(bus, [&bus, p = std::move(p)](const EventEnvelopeView& v) {
            if (!p) {
                bus.deliver_to_listeners(v);
                return;
            }
            bool allow = false;
            try {
                allow = p(v);
            } catch (...) {
                allow = false;
            }
            if (allow) {
                bus.deliver_to_listeners(v);
            }
        });
    }

    void attach_with_sink(Bus& bus, InboundSink sink) override {
        Predicate p = inbound_;
        inner_->attach_with_sink(
            bus, [sink = std::move(sink), p = std::move(p)](const EventEnvelopeView& v) {
                if (!p) {
                    if (sink)
                        sink(v);
                    return;
                }
                bool allow = false;
                try {
                    allow = p(v);
                } catch (...) {
                    allow = false;
                }
                if (allow && sink) {
                    sink(v);
                }
            });
    }

    void detach() noexcept override {
        inner_->detach();
    }

    void dispatch(const EventEnvelopeView& v) override {
        if (outbound_) {
            bool allow = false;
            try {
                allow = outbound_(v);
            } catch (...) {
                allow = false;
            }
            if (!allow) {
                return;
            }
        }
        inner_->dispatch(v);
    }

    void flush() override {
        inner_->flush();
    }

    [[nodiscard]] const std::shared_ptr<Transport>& inner() const noexcept {
        return inner_;
    }

private:
    std::shared_ptr<Transport> inner_;
    Predicate outbound_;
    Predicate inbound_;
};

}  // namespace conduit
