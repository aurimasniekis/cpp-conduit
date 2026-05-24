/// @file
/// @brief End-to-end AMQP smoke test. Skipped via GTEST_SKIP() when no broker
///        is reachable, so the suite stays green without one.
///
/// Set the broker via the env var `CONDUIT_AMQP_TEST_BROKER`
/// (default `amqp://guest:guest@localhost:5672/`).

#include <conduit/amqp/transport.hpp>
#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
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
    if (const char* env = std::getenv("CONDUIT_AMQP_TEST_BROKER"); env != nullptr) {
        return env;
    }
    return "amqp://guest:guest@localhost:5672/";
}

TEST(AmqpSmoke, PublishSubscribeRoundTrip) {
    conduit::amqp::Config pub_cfg;
    pub_cfg.url = broker_url();
    pub_cfg.connection_name = "conduit-test-pub";
    pub_cfg.publisher_confirms = true;

    conduit::amqp::Config sub_cfg = pub_cfg;
    sub_cfg.connection_name = "conduit-test-sub";

    conduit::Bus pub_bus;
    conduit::Bus sub_bus;

    conduit::amqp::Transport* pub_t = nullptr;
    conduit::amqp::Transport* sub_t = nullptr;
    try {
        pub_t = &pub_bus.use_transport<conduit::amqp::Transport>(pub_cfg);
        sub_t = &sub_bus.use_transport<conduit::amqp::Transport>(sub_cfg);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "AMQP broker unreachable at " << broker_url() << ": " << e.what();
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

    // Give the broker a moment to register the binding.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    pub_bus.publish(
        conduit::event(DeviceConnected{"dev-1"}).flag<conduit::flags::RequireAck>().build());

    for (int i = 0; i < 50 && hits == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    EXPECT_GE(hits.load(), 1);
    EXPECT_EQ(received_id, "dev-1");
}

}  // namespace
