/// Demonstrates `conduit::FilteredTransport`: wrap any transport with
/// per-leg predicates so events flow through only when the predicate says so.
/// Predicates are arbitrary — here we gate by event-name glob.

#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/filtered_transport.hpp>
#include <conduit/glob.hpp>
#include <conduit/relay/transport.hpp>

#include <iostream>
#include <memory>
#include <string>

#include <parcel/parcel.h>

struct OrderCreated : conduit::Event<OrderCreated, "order.created"> {
    std::string id;
    OrderCreated() = default;
    explicit OrderCreated(std::string s) : id(std::move(s)) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<OrderCreated>& b) {
        return b.field<&OrderCreated::id>("id");
    }
};

struct AuditLog : conduit::Event<AuditLog, "audit.log"> {
    std::string msg;
    AuditLog() = default;
    explicit AuditLog(std::string s) : msg(std::move(s)) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<AuditLog>& b) {
        return b.field<&AuditLog::msg>("msg");
    }
};

int main() {
    conduit::Bus bus;

    auto inner = std::make_shared<conduit::relay::Transport>(
        [](const conduit::EventEnvelopeView& v) { std::cout << "relayed: " << v.name() << '\n'; });

    // Only let "order.*" events flow outbound through the relay.
    bus.use_transport<conduit::FilteredTransport>(
        inner,
        /*outbound=*/[](const conduit::EventEnvelopeView& v) {
            return conduit::Glob::match("order.*", v.name());
        });

    bus.publish(conduit::event(OrderCreated{"ord-1"}).build());
    bus.publish(conduit::event(AuditLog{"hidden"}).build());

    return 0;
}
