/// @file
/// @brief End-to-end NATS smoke test. Skipped via GTEST_SKIP() when no NATS
///        server is reachable, so the suite stays green without one.
///
/// Set the broker via the env var `CONDUIT_NATS_TEST_BROKER`
/// (default `nats://localhost:4222`).

#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/nats/transport.hpp>
#include <conduit/serialization.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include <parcel/parcel.h>

namespace {

struct DeviceConnected : conduit::Event<DeviceConnected, "device.alpha.connected"> {
    std::string device_id;
    DeviceConnected() = default;
    explicit DeviceConnected(std::string id) : device_id(std::move(id)) {}

    [[maybe_unused]] static auto&
    event_field_descriptors(parcel::FieldsBuilder<DeviceConnected>& b) {
        return b.field<&DeviceConnected::device_id>("device_id");
    }
};

std::string broker_url() {
    if (const char* env = std::getenv("CONDUIT_NATS_TEST_BROKER"); env != nullptr) {
        return env;
    }
    return "nats://localhost:4222";
}

TEST(NatsSmoke, PublishSubscribeRoundTrip) {
    conduit::nats::Config pub_cfg;
    pub_cfg.url = broker_url();
    pub_cfg.name = "conduit-test-pub";
    pub_cfg.subject = "conduit.test.smoke";

    conduit::nats::Config sub_cfg = pub_cfg;
    sub_cfg.name = "conduit-test-sub";

    conduit::Bus pub_bus;
    conduit::Bus sub_bus;

    conduit::nats::Transport* pub_t = nullptr;
    conduit::nats::Transport* sub_t = nullptr;
    try {
        pub_t = &pub_bus.use_transport<conduit::nats::Transport>(pub_cfg);
        sub_t = &sub_bus.use_transport<conduit::nats::Transport>(sub_cfg);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "NATS server unreachable at " << broker_url() << ": " << e.what();
    }
    ASSERT_TRUE(pub_t->is_connected());
    ASSERT_TRUE(sub_t->is_connected());

    std::atomic<int> hits{0};
    std::string received_id;
    auto sub = sub_bus.listen<DeviceConnected>([&](const DeviceConnected& d) {
        received_id = d.device_id;
        ++hits;
    });
    (void)sub_t;

    // Give the server a moment after subscription.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    pub_bus.publish(
        conduit::event(DeviceConnected{"dev-1"}).flag<conduit::flags::RequireAck>().build());

    for (int i = 0; i < 50 && hits == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    EXPECT_GE(hits.load(), 1);
    EXPECT_EQ(received_id, "dev-1");
}

}  // namespace
