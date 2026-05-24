/// Multi-endpoint ZMQ example.
///
/// Routes different events onto different endpoints by attaching two
/// `zmq::Transport` instances wrapped in `FilteredTransport`.

#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/filtered_transport.hpp>
#include <conduit/glob.hpp>
#include <conduit/zmq/transport.hpp>

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
    const std::string orders_ep = "tcp://127.0.0.1:25560";
    const std::string audit_ep = "tcp://127.0.0.1:25561";

    conduit::zmq::Config orders_cfg;
    orders_cfg.pattern = conduit::zmq::Pattern::PubSub;
    orders_cfg.pub_endpoint = orders_ep;
    orders_cfg.pub_role = conduit::zmq::Role::Bind;

    conduit::zmq::Config audit_cfg;
    audit_cfg.pattern = conduit::zmq::Pattern::PubSub;
    audit_cfg.pub_endpoint = audit_ep;
    audit_cfg.pub_role = conduit::zmq::Role::Bind;

    conduit::Bus bus;

    try {
        auto orders_inner = std::make_shared<conduit::zmq::Transport>(orders_cfg);
        auto audit_inner = std::make_shared<conduit::zmq::Transport>(audit_cfg);

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
        std::cerr << "ZMQ unavailable: " << e.what() << '\n';
        return 0;
    }

    bus.publish(conduit::event(OrderCreated{"ord-1"}).build());
    bus.publish(conduit::event(AuditLog{"login-ok"}).build());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return 0;
}
