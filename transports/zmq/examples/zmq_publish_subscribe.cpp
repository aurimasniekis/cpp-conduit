/// Demonstrates the ZeroMQ transport with the PUB/SUB pattern.
///
/// One bus publishes on a bound PUB socket, another subscribes by connecting
/// to the same endpoint. The whole demo runs in-process — no broker needed.

#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/serialization.hpp>
#include <conduit/zmq/transport.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <parcel/parcel.h>

struct DeviceConnected : conduit::Event<DeviceConnected, "device.alpha.connected"> {
    std::string device_id;
    DeviceConnected() = default;
    explicit DeviceConnected(std::string id) : device_id(std::move(id)) {}

    [[maybe_unused]] static auto&
    event_field_descriptors(parcel::FieldsBuilder<DeviceConnected>& b) {
        return b.field<&DeviceConnected::device_id>("device_id");
    }
};

int main() {
    const char* env_ep = std::getenv("CONDUIT_ZMQ_ENDPOINT");
    const std::string endpoint = (env_ep != nullptr) ? env_ep : "tcp://127.0.0.1:25558";

    conduit::zmq::Config pub_cfg;
    pub_cfg.pattern = conduit::zmq::Pattern::PubSub;
    pub_cfg.pub_endpoint = endpoint;
    pub_cfg.pub_role = conduit::zmq::Role::Bind;

    conduit::zmq::Config sub_cfg;
    sub_cfg.pattern = conduit::zmq::Pattern::PubSub;
    sub_cfg.sub_endpoint = endpoint;
    sub_cfg.sub_role = conduit::zmq::Role::Connect;

    conduit::Bus pub_bus;
    conduit::Bus sub_bus;

    try {
        (void)pub_bus.use_transport<conduit::zmq::Transport>(pub_cfg);
        (void)sub_bus.use_transport<conduit::zmq::Transport>(sub_cfg);
    } catch (const std::exception& e) {
        std::cerr << "ZMQ endpoint " << endpoint << " unusable: " << e.what() << '\n';
        return 0;
    }

    std::atomic<int> hits{0};
    auto sub = sub_bus.listen<DeviceConnected>([&](const DeviceConnected& d) {
        std::cout << "received device_id=" << d.device_id << '\n';
        ++hits;
    });

    // Slow-joiner: give SUB time to register the subscription before PUB sends.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    pub_bus.publish(conduit::event(DeviceConnected{"dev-1"}).build());

    for (int i = 0; i < 30 && hits == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}
