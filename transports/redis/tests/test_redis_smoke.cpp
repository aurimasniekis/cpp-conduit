/// @file
/// @brief End-to-end Redis pub/sub smoke test. Skipped via GTEST_SKIP() when
///        no Redis is reachable, so the suite stays green without one.
///
/// Set the broker via the env var `CONDUIT_REDIS_TEST_BROKER`
/// (default `tcp://localhost:6379`).

#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/redis/transport.hpp>
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
    if (const char* env = std::getenv("CONDUIT_REDIS_TEST_BROKER"); env != nullptr) {
        return env;
    }
    return "tcp://localhost:6379";
}

TEST(RedisSmoke, PublishSubscribeRoundTrip) {
    conduit::redis::Config pub_cfg;
    pub_cfg.url = broker_url();
    pub_cfg.channel = "conduit:test:smoke";

    conduit::redis::Config sub_cfg = pub_cfg;

    conduit::Bus pub_bus;
    conduit::Bus sub_bus;

    conduit::redis::Transport* pub_t = nullptr;
    conduit::redis::Transport* sub_t = nullptr;
    try {
        pub_t = &pub_bus.use_transport<conduit::redis::Transport>(pub_cfg);
        sub_t = &sub_bus.use_transport<conduit::redis::Transport>(sub_cfg);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Redis unreachable at " << broker_url() << ": " << e.what();
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

    // Give Redis a moment to register the SUBSCRIBE.
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
