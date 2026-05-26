/// Demonstrates the MQTT transport: one bus publishes, another subscribes,
/// both connected to the same broker (default `tcp://localhost:1883`).

#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/mqtt/transport.hpp>
#include <conduit/serialization.hpp>

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
    const char* env_url = std::getenv("CONDUIT_MQTT_BROKER");
    const std::string broker = (env_url != nullptr) ? env_url : "tcp://localhost:1883";

    conduit::mqtt::Config pub_cfg;
    pub_cfg.url = broker;
    pub_cfg.client_id = "conduit-example-pub";

    conduit::mqtt::Config sub_cfg = pub_cfg;
    sub_cfg.client_id = "conduit-example-sub";

    conduit::Bus pub_bus;
    conduit::Bus sub_bus;

    try {
        (void)pub_bus.use_transport<conduit::mqtt::Transport>(pub_cfg);
        (void)sub_bus.use_transport<conduit::mqtt::Transport>(sub_cfg);
    } catch (const std::exception& e) {
        std::cerr << "MQTT broker unreachable at " << broker << ": " << e.what() << "\n";
        return 0;
    }

    std::atomic<int> hits{0};
    auto sub = sub_bus.listen<DeviceConnected>([&](const DeviceConnected& d) {
        std::cout << "received device_id=" << d.device_id << "\n";
        ++hits;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    pub_bus.publish(conduit::event(DeviceConnected{"dev-1"}).build());

    for (int i = 0; i < 30 && hits == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}
