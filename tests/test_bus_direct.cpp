#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/local/transport.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <stdexcept>

#include <parcel/parcel.h>

namespace {

struct Sig : conduit::Event<Sig, "sig"> {
    int n = 0;
    Sig() = default;
    explicit Sig(const int v) : n(v) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Sig>& b) {
        return b.field<&Sig::n>("n");
    }
};

TEST(BusDirect, SyncDispatch) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>(conduit::local::Execution::Direct);
    int seen = -1;
    auto sub = bus.listen<Sig>([&](const Sig& s) { seen = s.n; });
    bus.publish(conduit::event(Sig{99}).build());
    EXPECT_EQ(seen, 99);
}

TEST(BusDirect, ExceptionFromListenerDoesNotPropagate) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    auto sub = bus.listen<Sig>([](const Sig&) { throw std::runtime_error("x"); });
    auto sub2_called = std::make_shared<bool>(false);
    auto sub2 = bus.listen<Sig>([&](const Sig&) { *sub2_called = true; });

    EXPECT_NO_THROW(bus.publish(conduit::event(Sig{1}).build()));
    EXPECT_TRUE(*sub2_called);
}

TEST(BusDirect, PayloadShortcutForm) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    int seen = -1;
    auto sub = bus.listen<Sig>([&](const Sig& s) { seen = s.n; });
    Sig s;
    s.n = 42;
    bus.publish(s);
    EXPECT_EQ(seen, 42);
}

}  // namespace
