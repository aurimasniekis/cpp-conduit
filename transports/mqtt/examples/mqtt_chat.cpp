/// Multi-instance MQTT chat example.
///
/// Each process picks a node name from argv[1] and broadcasts a `chat.message`
/// event every two seconds. Every instance also subscribes to `chat.message`
/// and prints what it observes from the broker — including its own messages
/// echoed back. Run several copies in parallel against the same broker to see
/// them exchange traffic.
///
/// See `mosquitto.conf` next to this file for a ready-to-use broker config.

#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/mqtt/transport.hpp>
#include <conduit/serialization.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include <parcel/parcel.h>

struct ChatMessage : conduit::Event<ChatMessage, "chat.message"> {
    std::string from;
    std::uint64_t seq = 0;
    std::string text;

    ChatMessage() = default;
    ChatMessage(std::string f, const std::uint64_t s, std::string t)
        : from(std::move(f)), seq(s), text(std::move(t)) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<ChatMessage>& b) {
        return b.field<&ChatMessage::from>("from")
            .field<&ChatMessage::seq>("seq")
            .field<&ChatMessage::text>("text");
    }
};

namespace {
std::atomic<bool> g_stop{false};
void handle_sigint(int /*signo*/) {
    g_stop.store(true);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <node-name> [message-text]\n";
        return 2;
    }
    const std::string node = argv[1];
    const std::string text = (argc >= 3) ? argv[2] : ("hello from " + node);

    std::signal(SIGINT, handle_sigint);

    const char* env_url = std::getenv("CONDUIT_MQTT_BROKER");
    const std::string broker = (env_url != nullptr) ? env_url : "tcp://localhost:1883";

    conduit::mqtt::Config cfg;
    cfg.url = broker;
    cfg.client_id = "conduit-chat-" + node;

    conduit::Bus bus;
    try {
        (void)bus.use_transport<conduit::mqtt::Transport>(cfg);
    } catch (const std::exception& e) {
        std::cerr << "[" << node << "] MQTT broker unreachable at " << broker << ": " << e.what()
                  << '\n';
        return 1;
    }

    // listen<T> registers ChatMessage with the bus's shared event registry;
    // the MQTT transport reuses the same registry to decode inbound messages
    // arriving on the topic it is bound to ("conduit/envelope" by default).
    auto sub = bus.listen<ChatMessage>([&](const ChatMessage& m) {
        const bool is_self = (m.from == node);
        std::cout << "[" << node << "] " << (is_self ? "(self) " : "") << m.from << " #" << m.seq
                  << ": " << m.text << '\n';
    });

    // Give the broker a moment after subscription before we publish.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "[" << node << "] connected to " << broker
              << "; broadcasting every 2s (Ctrl-C to stop)\n";

    std::uint64_t seq = 0;
    while (!g_stop.load()) {
        std::ostringstream payload_text;
        payload_text << text << " (#" << seq << ")";
        bus.publish(conduit::event(ChatMessage{node, seq, payload_text.str()}).build());
        ++seq;
        for (int i = 0; i < 20 && !g_stop.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    std::cout << "[" << node << "] shutting down after " << seq << " message(s)\n";
    return 0;
}
