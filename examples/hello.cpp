/// Hello-world: define an event, listen for it, publish one.

#include <conduit/conduit.hpp>

#include <iostream>
#include <string>

#include <parcel/parcel.h>

struct Greeted : conduit::Event<Greeted, "greeted"> {
    std::string who;
    Greeted() = default;
    explicit Greeted(std::string s) : who(std::move(s)) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Greeted>& b) {
        return b.field<&Greeted::who>("who");
    }
};

int main() {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();

    auto sub =
        bus.listen<Greeted>([](const Greeted& g) { std::cout << "hello, " << g.who << '\n'; });

    bus.publish(conduit::event(Greeted{"world"}).build());
    return 0;
}
