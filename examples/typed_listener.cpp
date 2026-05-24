/// Typed listener: receive the `EventEnvelope&` form to access id/metadata
/// alongside the typed payload.

#include <conduit/conduit.hpp>

#include <iostream>
#include <string>

#include <parcel/parcel.h>

struct OrderCreated : conduit::Event<OrderCreated, "order.created"> {
    std::string order_id;
    double total = 0.0;
    OrderCreated() = default;
    OrderCreated(std::string id, const double t) : order_id(std::move(id)), total(t) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<OrderCreated>& b) {
        return b.field<&OrderCreated::order_id>("order_id").field<&OrderCreated::total>("total");
    }
};

int main() {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();

    auto sub = bus.listen<OrderCreated>([](const conduit::EventEnvelope& env) {
        const auto p = env.payload_as<OrderCreated>();
        std::cout << "id=" << env.id().string() << " order_id=" << p->order_id
                  << " total=" << p->total << '\n';
    });

    bus.publish(conduit::event(OrderCreated{"abc", 49.99}).metadata("tenant", "acme").build());
    return 0;
}
