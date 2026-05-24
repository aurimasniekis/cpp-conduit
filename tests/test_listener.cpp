#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/listener.hpp>
#include <conduit/local/transport.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>

#include <parcel/parcel.h>

namespace {

struct E : conduit::Event<E, "e"> {
    int n = 0;
    E() = default;
    explicit E(const int v) : n(v) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<E>& b) {
        return b.field<&E::n>("n");
    }
};

struct F : conduit::Event<F, "f"> {
    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<F>& b) {
        return b;
    }
};

TEST(Listener, RAIIUnregistersOnDestruction) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    std::atomic<int> count{0};
    {
        auto sub = bus.listen<E>([&](const E&) { ++count; });
        bus.publish(conduit::event(E{1}).build());
    }  // sub goes out of scope
    bus.publish(conduit::event(E{2}).build());
    EXPECT_EQ(count, 1);
}

TEST(Listener, MoveTransfersOwnership) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    std::atomic<int> count{0};
    auto sub_outer = std::make_unique<conduit::Subscription>();
    {
        auto sub = bus.listen<E>([&](const E&) { ++count; });
        *sub_outer = std::move(sub);
    }
    bus.publish(conduit::event(E{1}).build());
    EXPECT_EQ(count, 1);
    sub_outer.reset();
    bus.publish(conduit::event(E{2}).build());
    EXPECT_EQ(count, 1);
}

TEST(Listener, EnvelopeFormHandler) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    int seen = 0;
    auto sub = bus.listen<E>([&](const conduit::EventEnvelope& env) {
        const auto p = env.payload_as<E>();
        ASSERT_NE(p, nullptr);
        seen = p->n;
    });
    bus.publish(conduit::event(E{7}).build());
    EXPECT_EQ(seen, 7);
}

TEST(Listener, PatternListenerFiresOnMatch) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    std::atomic<int> hits{0};
    auto sub = bus.listen("e*", [&](const conduit::EventEnvelopeView&) { ++hits; });
    bus.publish(conduit::event(E{1}).build());
    bus.publish(conduit::event(F{}).build());
    EXPECT_EQ(hits, 1);
}

class CountingListener : public conduit::EventListener<E> {
public:
    std::atomic<int> count{0};
    void on_event(const E&) override {
        ++count;
    }
};

TEST(Listener, ClassBasedEventListenerWorks) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    auto l = std::make_shared<CountingListener>();
    auto sub = bus.listen<E>(l);
    bus.publish(conduit::event(E{1}).build());
    bus.publish(conduit::event(E{2}).build());
    EXPECT_EQ(l->count, 2);
}

}  // namespace
