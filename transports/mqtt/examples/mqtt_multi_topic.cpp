/// Multi-topic MQTT example.
///
/// Demonstrates the "transport = pipe" model: each MQTT topic is its own
/// `mqtt::Transport` instance. Wrapping each with `FilteredTransport` keeps
/// the two streams segregated by event-name glob, so an `OrderCreated`
/// envelope only ever leaves on the "orders" topic and `AuditLog` only on
/// the "audit" topic — even though both are published on the same bus.
///
/// Pair this with `mosquitto_sub -t 'conduit/orders' -t 'conduit/audit' -v`
/// to watch the two topics independently.

#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/filtered_transport.hpp>
#include <conduit/glob.hpp>
#include <conduit/mqtt/transport.hpp>

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
    const char* env_url = std::getenv("CONDUIT_MQTT_BROKER");
    const std::string broker = (env_url != nullptr) ? env_url : "tcp://localhost:1883";

    conduit::mqtt::Config orders_cfg;
    orders_cfg.url = broker;
    orders_cfg.client_id = "conduit-multi-orders";
    orders_cfg.topic = "conduit/orders";

    conduit::mqtt::Config audit_cfg;
    audit_cfg.url = broker;
    audit_cfg.client_id = "conduit-multi-audit";
    audit_cfg.topic = "conduit/audit";

    conduit::Bus bus;

    try {
        auto orders_inner = std::make_shared<conduit::mqtt::Transport>(orders_cfg);
        auto audit_inner = std::make_shared<conduit::mqtt::Transport>(audit_cfg);

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
        std::cerr << "MQTT broker unreachable at " << broker << ": " << e.what() << '\n';
        return 0;
    }

    bus.publish(conduit::event(OrderCreated{"ord-1"}).build());
    bus.publish(conduit::event(AuditLog{"login-ok"}).build());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return 0;
}
