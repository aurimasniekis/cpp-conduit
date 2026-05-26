/// NATS queue-group example.
///
/// Two subscribers join the same queue group `"workers"` on the
/// `conduit.tasks` subject; NATS load-balances each message to exactly one of
/// them. Useful for fan-out workers.
///
/// Run several copies in parallel and watch them share the workload.

#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/nats/transport.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include <parcel/parcel.h>

struct Task : conduit::Event<Task, "task.work"> {
    std::string id;
    std::string payload;
    Task() = default;
    Task(std::string i, std::string p) : id(std::move(i)), payload(std::move(p)) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Task>& b) {
        return b.field<&Task::id>("id").field<&Task::payload>("payload");
    }
};

namespace {
std::atomic<bool> g_stop{false};
void handle_sigint(int /*signo*/) {
    g_stop.store(true);
}
}  // namespace

int main(const int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <worker-name>\n";
        return 2;
    }
    const std::string worker = argv[1];

    (void)std::signal(SIGINT, handle_sigint);

    const char* env_url = std::getenv("CONDUIT_NATS_BROKER");
    const std::string broker = (env_url != nullptr) ? env_url : "nats://localhost:4222";

    conduit::nats::Config cfg;
    cfg.url = broker;
    cfg.name = "conduit-worker-" + worker;
    cfg.subject = "conduit.tasks";
    cfg.queue_group = "workers";

    conduit::Bus bus;
    try {
        (void)bus.use_transport<conduit::nats::Transport>(cfg);
    } catch (const std::exception& e) {
        std::cerr << "[" << worker << "] NATS server unreachable at " << broker << ": " << e.what()
                  << '\n';
        return 1;
    }

    auto sub = bus.listen<Task>([&](const Task& t) {
        std::cout << "[" << worker << "] picked task " << t.id << ": " << t.payload << '\n';
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    if (worker == "publisher") {
        for (std::uint64_t i = 0; i < 10 && !g_stop.load(); ++i) {
            bus.publish(
                conduit::event(Task{"task-" + std::to_string(i), "payload-" + std::to_string(i)})
                    .build());
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    } else {
        for (int i = 0; i < 50 && !g_stop.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return 0;
}
