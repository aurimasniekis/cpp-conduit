/// Demonstrates conduit's always-on prom metrics: the process-wide "conduit"
/// scope and the conduit_* families it owns.
///
/// Metrics are on by default through prom's no-op NullAdapter, so this example
/// reads no values — install a backend (e.g. prom::scrape::ScrapeAdapter or the
/// prometheus-cpp module) via `prom::Registry::global()->set_adapter(...)` to do
/// that. Here we exercise a bus so every conduit_* family gets created, then
/// fetch the scope and enumerate what conduit registered.

#include <conduit/conduit.hpp>

#include <prom/prom.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <parcel/parcel.h>

struct Order : conduit::Event<Order, "order.created"> {
    int id = 0;
    Order() = default;
    explicit Order(const int v) : id(v) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Order>& b) {
        return b.field<&Order::id>("id");
    }
};

/// Vetoes every envelope so conduit_events_dropped_total moves.
struct DropAll : conduit::Middleware {
    bool before_dispatch(conduit::EventEnvelopeView&) override {
        return false;
    }
};

int main() {
    {
        conduit::Bus bus;

        // A listener that throws on one event → conduit_listener_errors_total.
        auto sub = bus.listen<Order>([](const Order& o) {
            if (o.id == 0) {
                throw std::runtime_error("boom");
            }
        });

        // 3 publishes → conduit_events_published_total / listener_invocations /
        // dispatch_seconds (all labelled {event="order.created"}).
        for (int i = 0; i < 3; ++i) {
            bus.publish(conduit::event(Order{i}).build());
        }

        // Surface a transport-level failure →
        // conduit_transport_errors_total{transport="demo"}.
        bus.report_transport_error("demo",
                                   std::make_exception_ptr(std::runtime_error("decode failed")));

        // A drop-all middleware vetoes the next publish → conduit_events_dropped_total.
        bus.use_middleware<DropAll>();
        bus.publish(conduit::event(Order{42}).build());

        bus.drain();
    }

    // The "conduit" scope is registered process-wide the first time any metric is
    // touched; it outlives any individual bus.
    const auto scope = prom::find_scope("conduit");
    if (!scope) {
        std::cerr << "conduit scope not found — no metrics were recorded\n";
        return 1;
    }

    std::cout << R"(scope "conduit" (prefix ")" << scope->prefix() << R"(") owns these families:)";

    std::vector<std::string> lines;
    for (const auto& m : scope->metrics()) {
        lines.push_back("  " + std::string(prom::to_string(m.type)) + "  " + m.name);
    }
    std::ranges::sort(lines);
    for (const auto& line : lines) {
        std::cout << line << '\n';
    }
    std::cout << "(install a prom adapter to read the recorded values)\n";
    return 0;
}
