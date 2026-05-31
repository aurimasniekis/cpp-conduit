#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/local/transport.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include <parcel/parcel.h>

namespace {

struct P : conduit::Event<P, "p"> {
    int n = 0;
    P() = default;
    explicit P(const int v) : n(v) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<P>& b) {
        return b.field<&P::n>("n");
    }
};

TEST(BusThreadPool, AllPublishedEventsObservedAfterDrain) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>(
        conduit::local::Execution::ThreadPool, conduit::local::ThreadPoolConfig{.max_workers = 4});

    std::atomic<int> count{0};
    auto sub = bus.listen<P>([&](const P&) { ++count; });

    constexpr int n = 200;
    for (int i = 0; i < n; ++i) {
        bus.publish(conduit::event(P{i}).build());
    }
    bus.drain();
    EXPECT_EQ(count, n);
}

TEST(BusThreadPool, ShutdownIsSafe) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>(
        conduit::local::Execution::ThreadPool, conduit::local::ThreadPoolConfig{.max_workers = 2});
    auto sub =
        bus.listen<P>([&](const P&) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });
    for (int i = 0; i < 50; ++i) {
        bus.publish(conduit::event(P{i}).build());
    }
    EXPECT_NO_THROW(bus.shutdown());
}

TEST(BusThreadPool, BoundedQueueBlocksProducer) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>(
        conduit::local::Execution::ThreadPool,
        conduit::local::ThreadPoolConfig{.max_workers = 1, .max_queue_size = 4});

    std::atomic<int> got{0};
    auto sub = bus.listen<P>([&](const P&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ++got;
    });
    for (int i = 0; i < 30; ++i) {
        bus.publish(conduit::event(P{i}).build());
    }
    bus.drain();
    EXPECT_EQ(got, 30);
}

}  // namespace
