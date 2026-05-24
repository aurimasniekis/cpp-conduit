/// relay::Transport hooks selected events out to a caller-supplied callback —
/// the same shape you'd point at a websocket or HTTP sink.

#include <conduit/conduit.hpp>

#include <iostream>
#include <string>

#include <parcel/parcel.h>

struct Order : conduit::Event<Order, "order.created"> {
    std::string id;
    Order() = default;
    explicit Order(std::string s) : id(std::move(s)) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Order>& b) {
        return b.field<&Order::id>("id");
    }
};
struct Audit : conduit::Event<Audit, "audit.recorded"> {
    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Audit>& b) {
        return b;
    }
};

int main() {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();

    bus.use_transport<conduit::relay::Transport>(
        "order.*", [](const conduit::EventEnvelopeView& v) {
            const auto j = conduit::serialization::encode_json(v);
            std::cout << "relayed: " << j.dump() << '\n';
        });

    bus.publish(conduit::event(Order{"O-9"}).build());
    bus.publish(conduit::event(Audit{}).build());  // not relayed
    return 0;
}
