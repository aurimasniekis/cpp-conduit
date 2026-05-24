#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/filtered_transport.hpp>
#include <conduit/glob.hpp>
#include <conduit/transport.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <parcel/parcel.h>

namespace {

struct OrderCreated : conduit::Event<OrderCreated, "order.created"> {
    std::string id;
    OrderCreated() = default;
    explicit OrderCreated(std::string s) : id(std::move(s)) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<OrderCreated>& b) {
        return b.field<&OrderCreated::id>("id");
    }
};

struct AuditLog : conduit::Event<AuditLog, "audit.log"> {
    std::string msg;
    AuditLog() = default;
    explicit AuditLog(std::string s) : msg(std::move(s)) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<AuditLog>& b) {
        return b.field<&AuditLog::msg>("msg");
    }
};

/// A fake transport that records every envelope passed through `dispatch()`
/// and exposes a method to inject inbound envelopes via the sink installed
/// at attach time.
class Recorder : public conduit::Transport {
public:
    explicit Recorder(const conduit::TransportScope scope = conduit::TransportScope::Local)
        : scope_(scope) {}

    [[nodiscard]] conduit::TransportScope scope() const noexcept override {
        return scope_;
    }

    void dispatch(const conduit::EventEnvelopeView& v) override {
        std::scoped_lock lock(mu_);
        out_.emplace_back(v.name());
    }

    void inject(const conduit::EventEnvelopeView& v) const {
        deliver_inbound(v);
    }

    [[nodiscard]] std::vector<std::string> outbound() const {
        std::scoped_lock lock(mu_);
        return out_;
    }

private:
    mutable std::mutex mu_;
    std::vector<std::string> out_;
    conduit::TransportScope scope_;
};

TEST(FilteredTransport, OutboundOnlyFilterDropsByName) {
    auto rec = std::make_shared<Recorder>();
    conduit::Bus bus;
    bus.use_transport<conduit::FilteredTransport>(
        rec,
        /*outbound=*/[](const conduit::EventEnvelopeView& v) {
            return conduit::Glob::match("order.*", v.name());
        });

    bus.publish(conduit::event(OrderCreated{"x"}).build());
    bus.publish(conduit::event(AuditLog{"x"}).build());

    const auto recorded = rec->outbound();
    ASSERT_EQ(recorded.size(), 1u);
    EXPECT_EQ(recorded[0], "order.created");
}

TEST(FilteredTransport, InboundOnlyFilterGatesDelivery) {
    auto rec = std::make_shared<Recorder>();
    conduit::Bus bus;
    const auto& filtered = bus.use_transport<conduit::FilteredTransport>(
        rec,
        /*outbound=*/conduit::FilteredTransport::Predicate{},
        /*inbound=*/[](const conduit::EventEnvelopeView& v) { return v.name() != "audit.log"; });
    (void)filtered;

    std::atomic<int> orders{0};
    std::atomic<int> audits{0};
    auto s1 = bus.listen<OrderCreated>([&](const OrderCreated&) { ++orders; });
    auto s2 = bus.listen<AuditLog>([&](const AuditLog&) { ++audits; });

    rec->inject(conduit::event(OrderCreated{"x"}).build());
    rec->inject(conduit::event(AuditLog{"a"}).build());

    EXPECT_EQ(orders.load(), 1);
    EXPECT_EQ(audits.load(), 0);
}

TEST(FilteredTransport, BothLegsGated) {
    auto rec = std::make_shared<Recorder>();
    conduit::Bus bus;
    bus.use_transport<conduit::FilteredTransport>(
        rec,
        /*outbound=*/
        [](const conduit::EventEnvelopeView& v) { return v.name() == "order.created"; },
        /*inbound=*/
        [](const conduit::EventEnvelopeView& v) { return v.name() == "order.created"; });

    std::atomic<int> orders{0};
    std::atomic<int> audits{0};
    auto s1 = bus.listen<OrderCreated>([&](const OrderCreated&) { ++orders; });
    auto s2 = bus.listen<AuditLog>([&](const AuditLog&) { ++audits; });

    bus.publish(conduit::event(OrderCreated{"x"}).build());
    bus.publish(conduit::event(AuditLog{"a"}).build());

    auto recorded = rec->outbound();
    ASSERT_EQ(recorded.size(), 1u);
    EXPECT_EQ(recorded[0], "order.created");

    rec->inject(conduit::event(OrderCreated{"y"}).build());
    rec->inject(conduit::event(AuditLog{"b"}).build());
    EXPECT_EQ(orders.load(), 1);
    EXPECT_EQ(audits.load(), 0);
}

TEST(FilteredTransport, EmptyPredicatesPassThrough) {
    auto rec = std::make_shared<Recorder>();
    conduit::Bus bus;
    bus.use_transport<conduit::FilteredTransport>(rec);

    std::atomic<int> orders{0};
    auto s = bus.listen<OrderCreated>([&](const OrderCreated&) { ++orders; });

    bus.publish(conduit::event(OrderCreated{"x"}).build());
    bus.publish(conduit::event(AuditLog{"a"}).build());

    EXPECT_EQ(rec->outbound().size(), 2u);

    rec->inject(conduit::event(OrderCreated{"y"}).build());
    EXPECT_EQ(orders.load(), 1);
}

TEST(FilteredTransport, DropAllPredicateBlocksEverything) {
    auto rec = std::make_shared<Recorder>();
    conduit::Bus bus;
    bus.use_transport<conduit::FilteredTransport>(
        rec,
        /*outbound=*/[](const conduit::EventEnvelopeView&) { return false; },
        /*inbound=*/[](const conduit::EventEnvelopeView&) { return false; });

    std::atomic<int> orders{0};
    auto s = bus.listen<OrderCreated>([&](const OrderCreated&) { ++orders; });

    bus.publish(conduit::event(OrderCreated{"x"}).build());
    EXPECT_EQ(rec->outbound().size(), 0u);

    rec->inject(conduit::event(OrderCreated{"y"}).build());
    EXPECT_EQ(orders.load(), 0);
}

TEST(FilteredTransport, ThrowingPredicateIsTreatedAsDrop) {
    auto rec = std::make_shared<Recorder>();
    conduit::Bus bus;
    bus.use_transport<conduit::FilteredTransport>(
        rec,
        /*outbound=*/
        [](const conduit::EventEnvelopeView&) -> bool { throw std::runtime_error{"boom"}; },
        /*inbound=*/
        [](const conduit::EventEnvelopeView&) -> bool { throw std::runtime_error{"boom"}; });

    std::atomic<int> orders{0};
    auto s = bus.listen<OrderCreated>([&](const OrderCreated&) { ++orders; });

    bus.publish(conduit::event(OrderCreated{"x"}).build());
    EXPECT_EQ(rec->outbound().size(), 0u);

    rec->inject(conduit::event(OrderCreated{"y"}).build());
    EXPECT_EQ(orders.load(), 0);
}

TEST(FilteredTransport, ScopeForwardsToInner) {
    const auto remote_rec = std::make_shared<Recorder>(conduit::TransportScope::Remote);
    const conduit::FilteredTransport ft{remote_rec};
    EXPECT_EQ(ft.scope(), conduit::TransportScope::Remote);
}

}  // namespace
