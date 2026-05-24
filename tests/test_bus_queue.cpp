#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/flags.hpp>
#include <conduit/local/transport.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include <parcel/parcel.h>

namespace {

struct Q : conduit::Event<Q, "q"> {
    int n = 0;
    Q() = default;
    explicit Q(const int v) : n(v) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Q>& b) {
        return b.field<&Q::n>("n");
    }
};

TEST(BusQueue, AllDeliveredAfterDrain) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>(conduit::local::Execution::Queue);
    std::atomic<int> sum{0};
    auto sub = bus.listen<Q>([&](const Q& q) { sum += q.n; });
    for (int i = 1; i <= 10; ++i) {
        bus.publish(conduit::event(Q{i}).build());
    }
    bus.drain();
    EXPECT_EQ(sum, 55);
}

TEST(BusQueue, DirectFlagBypassesQueueAndRunsInline) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>(conduit::local::Execution::Queue);
    bool ran = false;
    const auto publisher_tid = std::this_thread::get_id();
    std::thread::id ran_tid;
    auto sub = bus.listen<Q>([&](const Q&) {
        ran = true;
        ran_tid = std::this_thread::get_id();
    });
    bus.publish(conduit::event(Q{1}).flag<conduit::flags::Direct>().build());
    EXPECT_TRUE(ran);
    EXPECT_EQ(ran_tid, publisher_tid);
}

}  // namespace
