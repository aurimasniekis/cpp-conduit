/// ThreadPool execution mode for the local transport.

#include <conduit/conduit.hpp>

#include <atomic>
#include <iostream>

#include <parcel/parcel.h>

struct Job : conduit::Event<Job, "job"> {
    int id = 0;
    Job() = default;
    explicit Job(const int v) : id(v) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Job>& b) {
        return b.field<&Job::id>("id");
    }
};

int main() {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>(conduit::local::Execution::ThreadPool,
                                                 conduit::local::ThreadPoolConfig{.threads = 4});

    std::atomic<int> count{0};
    auto sub = bus.listen<Job>([&](const Job& j) { count += j.id; });

    for (int i = 1; i <= 100; ++i) {
        bus.publish(conduit::event(Job{i}).build());
    }
    bus.drain();
    std::cout << "sum=" << count << '\n';
    return 0;
}
