/// EventSubscriber: one object that wires up many listeners.

#include <conduit/conduit.hpp>

#include <iostream>
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
struct OrderShipped : conduit::Event<OrderShipped, "order.shipped"> {
    std::string id;
    OrderShipped() = default;
    explicit OrderShipped(std::string s) : id(std::move(s)) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<OrderShipped>& b) {
        return b.field<&OrderShipped::id>("id");
    }
};

class OrderProjection : public conduit::EventSubscriber {
public:
    void register_to(conduit::Bus& bus) override {
        on<OrderCreated>(bus,
                         [](const OrderCreated& o) { std::cout << "created " << o.id << '\n'; });
        on<OrderShipped>(bus,
                         [](const OrderShipped& o) { std::cout << "shipped " << o.id << '\n'; });
    }
};

int main() {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    OrderProjection projection;
    bus.register_subscriber(projection);

    bus.publish(conduit::event(OrderCreated{"O-1"}).build());
    bus.publish(conduit::event(OrderShipped{"O-1"}).build());
    return 0;
}
