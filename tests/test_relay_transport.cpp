#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/local/transport.hpp>
#include <conduit/relay/transport.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <string>

#include <parcel/parcel.h>

namespace {

struct Ord : conduit::Event<Ord, "order.created"> {
    std::string id;
    Ord() = default;
    explicit Ord(std::string s) : id(std::move(s)) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Ord>& b) {
        return b.field<&Ord::id>("id");
    }
};

struct Other : conduit::Event<Other, "audit.something"> {
    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Other>& b) {
        return b;
    }
};

TEST(Relay, FiresOnPatternMatch) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    std::atomic<int> hits{0};
    bus.use_transport<conduit::relay::Transport>(
        "order.*", [&](const conduit::EventEnvelopeView&) { ++hits; });

    bus.publish(conduit::event(Ord{"x"}).build());
    bus.publish(conduit::event(Other{}).build());
    EXPECT_EQ(hits, 1);
}

TEST(Relay, MultipleRoutesEachFireIndependently) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    std::atomic<int> any{0};
    std::atomic<int> orders{0};
    auto& relay = bus.use_transport<conduit::relay::Transport>(
        "**", [&](const conduit::EventEnvelopeView&) { ++any; });
    (void)relay.add_route("order.*", [&](const conduit::EventEnvelopeView&) { ++orders; });

    bus.publish(conduit::event(Ord{"x"}).build());
    bus.publish(conduit::event(Other{}).build());
    EXPECT_EQ(any, 2);
    EXPECT_EQ(orders, 1);
}

TEST(Relay, RuntimeRemoveRoute) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    std::atomic<int> hits{0};
    auto& relay = bus.use_transport<conduit::relay::Transport>(
        "**", [&](const conduit::EventEnvelopeView&) { ++hits; });
    const auto id = relay.add_route("order.*", [&](const conduit::EventEnvelopeView&) { ++hits; });
    bus.publish(conduit::event(Ord{"x"}).build());
    EXPECT_EQ(hits, 2);

    relay.remove_route(id);
    bus.publish(conduit::event(Ord{"y"}).build());
    EXPECT_EQ(hits, 3);
}

}  // namespace
