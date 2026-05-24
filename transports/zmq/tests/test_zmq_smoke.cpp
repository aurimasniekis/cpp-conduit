/// @file
/// @brief End-to-end ZeroMQ smoke test. Uses loopback PUB/SUB with the
///        publisher binding and the subscriber connecting, so no broker is
///        needed.
///
/// Set the endpoint via the env var `CONDUIT_ZMQ_TEST_ENDPOINT`
/// (default `tcp://127.0.0.1:25557`).

#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/serialization.hpp>
#include <conduit/zmq/transport.hpp>

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

std::string endpoint_url() {
    if (const char* env = std::getenv("CONDUIT_ZMQ_TEST_ENDPOINT"); env != nullptr) {
        return env;
    }
    return "tcp://127.0.0.1:25557";
}

TEST(ZmqSmoke, PublishSubscribeRoundTrip) {
    const std::string endpoint = endpoint_url();

    // Publisher: PUB binds.
    conduit::zmq::Config pub_cfg;
    pub_cfg.pattern = conduit::zmq::Pattern::PubSub;
    pub_cfg.pub_endpoint = endpoint;
    pub_cfg.pub_role = conduit::zmq::Role::Bind;

    // Subscriber: SUB connects to the same endpoint.
    conduit::zmq::Config sub_cfg;
    sub_cfg.pattern = conduit::zmq::Pattern::PubSub;
    sub_cfg.sub_endpoint = endpoint;
    sub_cfg.sub_role = conduit::zmq::Role::Connect;

    conduit::Bus pub_bus;
    conduit::Bus sub_bus;

    conduit::zmq::Transport* pub_t = nullptr;
    conduit::zmq::Transport* sub_t = nullptr;
    try {
        pub_t = &pub_bus.use_transport<conduit::zmq::Transport>(pub_cfg);
        sub_t = &sub_bus.use_transport<conduit::zmq::Transport>(sub_cfg);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "ZMQ endpoint unusable at " << endpoint << ": " << e.what();
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

    // PUB/SUB needs a "slow joiner" delay so the SUB's TCP+subscribe handshake
    // is in place before the first message is sent — otherwise PUB drops it.
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
