/// flags::Direct forces in-thread delivery even when the local transport is in
/// Queue or ThreadPool mode.

#include <conduit/conduit.hpp>

#include <iostream>
#include <thread>

#include <parcel/parcel.h>

struct Beep : conduit::Event<Beep, "beep"> {
    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Beep>& b) {
        return b;
    }
};

int main() {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>(conduit::local::Execution::Queue);
    const auto publisher_tid = std::this_thread::get_id();

    auto sub = bus.listen<Beep>([&](const Beep&) {
        std::cout << (std::this_thread::get_id() == publisher_tid ? "inline\n" : "background\n");
    });

    // Without Direct: runs on the queue worker thread.
    bus.publish(conduit::event(Beep{}).build());
    // With Direct: runs in the publisher's thread.
    bus.publish(conduit::event(Beep{}).flag<conduit::flags::Direct>().build());

    bus.drain();
    return 0;
}
