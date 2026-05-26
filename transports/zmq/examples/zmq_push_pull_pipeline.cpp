/// ZMQ PUSH/PULL pipeline example.
///
/// One process pushes work, another (or several) pulls. The puller `bind`s,
/// the pusher `connect`s — ZMQ load-balances messages round-robin across
/// connected pushers (or pullers).
///
/// Run with argv[1] == "push" or "pull" to act as either side.

#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/zmq/transport.hpp>

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
    std::uint64_t id = 0;
    std::string payload;
    Task() = default;
    Task(const std::uint64_t i, std::string p) : id(i), payload(std::move(p)) {}

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
        std::cerr << "usage: " << argv[0] << " <push|pull>\n";
        return 2;
    }
    (void)std::signal(SIGINT, handle_sigint);

    const std::string mode = argv[1];
    const std::string endpoint = "tcp://127.0.0.1:25562";

    conduit::zmq::Config cfg;
    cfg.pattern = conduit::zmq::Pattern::PushPull;
    if (mode == "push") {
        cfg.push_endpoint = endpoint;
        cfg.push_role = conduit::zmq::Role::Connect;
    } else {
        cfg.pull_endpoint = endpoint;
        cfg.pull_role = conduit::zmq::Role::Bind;
    }

    conduit::Bus bus;
    try {
        (void)bus.use_transport<conduit::zmq::Transport>(cfg);
    } catch (const std::exception& e) {
        std::cerr << "ZMQ endpoint unusable: " << e.what() << '\n';
        return 1;
    }

    auto sub = bus.listen<Task>([&](const Task& t) {
        std::cout << "[pull] received task " << t.id << ": " << t.payload << '\n';
    });

    if (mode == "push") {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        for (std::uint64_t i = 0; i < 5 && !g_stop.load(); ++i) {
            bus.publish(conduit::event(Task{i, "payload-" + std::to_string(i)}).build());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    } else {
        for (int i = 0; i < 50 && !g_stop.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return 0;
}
