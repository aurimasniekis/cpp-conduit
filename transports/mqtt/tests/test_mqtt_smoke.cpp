/// @file
/// @brief End-to-end MQTT smoke test. Skipped via GTEST_SKIP() when no broker
///        is reachable, so the suite stays green without a local broker.
///
/// Set the broker via the env var `CONDUIT_MQTT_TEST_BROKER`
/// (default `tcp://localhost:1883`).

#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/mqtt/transport.hpp>
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
    if (const char* env = std::getenv("CONDUIT_MQTT_TEST_BROKER"); env != nullptr) {
        return env;
    }
    return "tcp://localhost:1883";
}

TEST(MqttSmoke, PublishSubscribeRoundTrip) {
    conduit::mqtt::Config pub_cfg;
    pub_cfg.url = broker_url();
    pub_cfg.client_id = "conduit-test-pub";

    conduit::mqtt::Config sub_cfg = pub_cfg;
    sub_cfg.client_id = "conduit-test-sub";

    conduit::Bus pub_bus;
    conduit::Bus sub_bus;

    conduit::mqtt::Transport* pub_t = nullptr;
    conduit::mqtt::Transport* sub_t = nullptr;
    try {
        pub_t = &pub_bus.use_transport<conduit::mqtt::Transport>(pub_cfg);
        sub_t = &sub_bus.use_transport<conduit::mqtt::Transport>(sub_cfg);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "MQTT broker unreachable at " << broker_url() << ": " << e.what();
    }
    ASSERT_TRUE(pub_t->is_connected());
    ASSERT_TRUE(sub_t->is_connected());

    std::atomic<int> hits{0};
    std::string received_id;
    // listen<T> registers DeviceConnected with the bus's shared registry.
    // The MQTT transport reuses that same registry to decode inbound
    // messages on the topic it was constructed with.
    auto sub = sub_bus.listen<DeviceConnected>([&](const DeviceConnected& d) {
        received_id = d.device_id;
        ++hits;
    });
    (void)sub_t;  // topic subscription was set up at attach()

    // Give the broker a moment to register the subscription.
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
