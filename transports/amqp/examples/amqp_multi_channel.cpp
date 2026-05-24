/// Multi-channel AMQP example.
///
/// Demonstrates the "transport = pipe" model: each AMQP routing key is its
/// own `amqp::Transport` instance. Wrapping each with `FilteredTransport`
/// keeps the two streams segregated by event-name glob, so an `OrderCreated`
/// envelope only ever leaves on the "orders" routing key and `AuditLog` only
/// on the "audit" routing key — even though both are published on the same
/// bus.

#include <conduit/amqp/transport.hpp>
#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/filtered_transport.hpp>
#include <conduit/glob.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

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
    const char* env_url = std::getenv("CONDUIT_AMQP_BROKER");
    const std::string broker =
        (env_url != nullptr) ? env_url : "amqp://guest:guest@localhost:5672/";

    conduit::amqp::Config orders_cfg;
    orders_cfg.url = broker;
    orders_cfg.connection_name = "conduit-multi-orders";
    orders_cfg.routing_key = "conduit.orders";

    conduit::amqp::Config audit_cfg;
    audit_cfg.url = broker;
    audit_cfg.connection_name = "conduit-multi-audit";
    audit_cfg.routing_key = "conduit.audit";

    conduit::Bus bus;

    try {
        auto orders_inner = std::make_shared<conduit::amqp::Transport>(orders_cfg);
        auto audit_inner = std::make_shared<conduit::amqp::Transport>(audit_cfg);

        bus.use_transport<conduit::FilteredTransport>(
            orders_inner,
            /*outbound=*/[](const conduit::EventEnvelopeView& v) {
                return conduit::Glob::match("order.*", v.name());
            });
        bus.use_transport<conduit::FilteredTransport>(
            audit_inner,
            /*outbound=*/[](const conduit::EventEnvelopeView& v) {
                return conduit::Glob::match("audit.*", v.name());
            });
    } catch (const std::exception& e) {
        std::cerr << "AMQP broker unreachable at " << broker << ": " << e.what() << '\n';
        return 0;
    }

    bus.publish(conduit::event(OrderCreated{"ord-1"}).build());
    bus.publish(conduit::event(AuditLog{"login-ok"}).build());

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    return 0;
}
