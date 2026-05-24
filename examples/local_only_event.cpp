/// Demonstrates that flags::LocalOnly keeps an event in-process even when a
/// "remote" transport is attached.

#include <conduit/conduit.hpp>

#include <atomic>
#include <iostream>
#include <string>

#include <parcel/parcel.h>

struct AppConfigReloadEvent : conduit::Event<AppConfigReloadEvent, "app.config.reload">,
                              conduit::DefaultFlags<conduit::flags::LocalOnly> {
    std::string source_path;
    AppConfigReloadEvent() = default;
    explicit AppConfigReloadEvent(std::string p) : source_path(std::move(p)) {}

    [[maybe_unused]] static auto&
    event_field_descriptors(parcel::FieldsBuilder<AppConfigReloadEvent>& b) {
        return b.field<&AppConfigReloadEvent::source_path>("source_path");
    }
};

class FakeRemote : public conduit::Transport {
public:
    std::atomic<int> count{0};
    conduit::TransportScope scope() const noexcept override {
        return conduit::TransportScope::Remote;
    }
    void dispatch(const conduit::EventEnvelopeView&) override {
        count++;
    }
};

int main() {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    const auto& remote = bus.use_transport<FakeRemote>();

    std::atomic<int> local_hits{0};
    auto sub = bus.listen<AppConfigReloadEvent>([&](const AppConfigReloadEvent&) { local_hits++; });

    bus.publish(conduit::event(AppConfigReloadEvent{"/etc/app.toml"}).build());

    std::cout << "local_hits=" << local_hits << " remote_count=" << remote.count << '\n';
    return 0;
}
