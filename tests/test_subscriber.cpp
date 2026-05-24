#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/listener.hpp>
#include <conduit/local/transport.hpp>

#include <gtest/gtest.h>

#include <atomic>

#include <parcel/parcel.h>

namespace {

struct EvA : conduit::Event<EvA, "a"> {
    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<EvA>& b) {
        return b;
    }
};

struct EvB : conduit::Event<EvB, "b"> {
    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<EvB>& b) {
        return b;
    }
};

class Multi : public conduit::EventSubscriber {
public:
    std::atomic<int> a_count{0};
    std::atomic<int> b_count{0};
    std::atomic<int> pattern_count{0};

    void register_to(conduit::Bus& bus) override {
        on<EvA>(bus, [this](const EvA&) { ++a_count; });
        on<EvB>(bus, [this](const EvB&) { ++b_count; });
        on(bus, "*", [this](const conduit::EventEnvelopeView&) { ++pattern_count; });
    }
};

TEST(Subscriber, RegistersMultipleListeners) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    Multi m;
    bus.register_subscriber(m);
    bus.publish(conduit::event(EvA{}).build());
    bus.publish(conduit::event(EvB{}).build());
    bus.publish(conduit::event(EvA{}).build());
    EXPECT_EQ(m.a_count, 2);
    EXPECT_EQ(m.b_count, 1);
    EXPECT_EQ(m.pattern_count, 3);
}

TEST(Subscriber, DestructorUnregisters) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    std::atomic<int> counted{0};
    auto sub = bus.listen<EvA>([&](const EvA&) { ++counted; });
    {
        Multi m;
        bus.register_subscriber(m);
        bus.publish(conduit::event(EvA{}).build());
        EXPECT_EQ(m.a_count, 1);
    }  // m goes out of scope; its subscriptions release
    bus.publish(conduit::event(EvA{}).build());
    EXPECT_EQ(counted, 2);
}

}  // namespace
