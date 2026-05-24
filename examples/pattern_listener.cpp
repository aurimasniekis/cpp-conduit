/// Pattern listener: subscribe to a glob.

#include <conduit/conduit.hpp>

#include <iostream>
#include <string>

#include <parcel/parcel.h>

struct OrderCreated : conduit::Event<OrderCreated, "order.created"> {
    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<OrderCreated>& b) {
        return b;
    }
};
struct OrderShipped : conduit::Event<OrderShipped, "order.shipped"> {
    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<OrderShipped>& b) {
        return b;
    }
};

int main() {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();

    auto sub = bus.listen("order.*", [](const conduit::EventEnvelopeView& v) {
        std::cout << "saw " << v.name() << '\n';
    });

    bus.publish(conduit::event(OrderCreated{}).build());
    bus.publish(conduit::event(OrderShipped{}).build());
    return 0;
}
