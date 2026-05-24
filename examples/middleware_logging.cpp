/// A logging middleware that records before/after dispatch.

#include <conduit/conduit.hpp>

#include <iostream>

#include <parcel/parcel.h>

struct Tick : conduit::Event<Tick, "tick"> {
    int n = 0;
    Tick() = default;
    explicit Tick(const int v) : n(v) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Tick>& b) {
        return b.field<&Tick::n>("n");
    }
};

class LoggingMW : public conduit::Middleware {
public:
    bool before_dispatch(conduit::EventEnvelopeView& v) override {
        std::cout << ">> " << v.name() << " id=" << v.id().string() << '\n';
        return true;
    }
    void after_dispatch(conduit::EventEnvelopeView& v) override {
        std::cout << "<< " << v.name() << '\n';
    }
};

int main() {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    bus.use_middleware<LoggingMW>();
    auto sub = bus.listen<Tick>([](const Tick& t) { std::cout << "   n=" << t.n << '\n'; });
    bus.publish(conduit::event(Tick{7}).build());
    return 0;
}
