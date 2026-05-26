#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/listener.hpp>
#include <conduit/local/transport.hpp>
#include <conduit/middleware.hpp>
#include <conduit/transport.hpp>

#include <gtest/gtest.h>

#include <commons/prioritized.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <parcel/parcel.h>

namespace {

struct Ping : conduit::Event<Ping, "priority.ping"> {
    int n = 0;
    Ping() = default;
    explicit Ping(const int v) : n(v) {}
    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Ping>& b) {
        return b.field<&Ping::n>("n");
    }
};

class RecorderMW : public conduit::Middleware {
public:
    RecorderMW(std::vector<std::string>* l, std::string n, const int p)
        : log_(l), name_(std::move(n)), priority_(p) {}

    bool before_dispatch(conduit::EventEnvelopeView&) override {
        log_->push_back(name_ + ".before");
        return true;
    }
    void after_dispatch(conduit::EventEnvelopeView&) override {
        log_->push_back(name_ + ".after");
    }
    [[nodiscard]] int priority() const noexcept override {
        return priority_;
    }

private:
    std::vector<std::string>* log_;
    std::string name_;
    int priority_;
};

TEST(Priority, MiddlewareOrderedByPriority) {
    std::vector<std::string> log;
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    // Insertion order: B (0), A (-10), C (10).
    // Expected before-order: A, B, C; after-order: C, B, A.
    bus.use_middleware<RecorderMW>(&log, "B", 0);
    bus.use_middleware<RecorderMW>(&log, "A", -10);
    bus.use_middleware<RecorderMW>(&log, "C", 10);

    bus.publish(conduit::event(Ping{1}).build());

    ASSERT_EQ(log.size(), 6U);
    EXPECT_EQ(log[0], "A.before");
    EXPECT_EQ(log[1], "B.before");
    EXPECT_EQ(log[2], "C.before");
    EXPECT_EQ(log[3], "C.after");
    EXPECT_EQ(log[4], "B.after");
    EXPECT_EQ(log[5], "A.after");
}

TEST(Priority, MiddlewareSamePriorityKeepsInsertionOrder) {
    std::vector<std::string> log;
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    bus.use_middleware<RecorderMW>(&log, "first", 0);
    bus.use_middleware<RecorderMW>(&log, "second", 0);
    bus.use_middleware<RecorderMW>(&log, "third", 0);

    bus.publish(conduit::event(Ping{1}).build());

    ASSERT_GE(log.size(), 3U);
    EXPECT_EQ(log[0], "first.before");
    EXPECT_EQ(log[1], "second.before");
    EXPECT_EQ(log[2], "third.before");
}

TEST(Priority, ListenerLambdaPriorityFromBusListen) {
    std::vector<std::string> log;
    conduit::Bus bus;

    auto sub_default = bus.listen<Ping>([&](const Ping&) { log.emplace_back("default"); });
    auto sub_high =
        bus.listen<Ping>([&](const Ping&) { log.emplace_back("high"); }, /*priority=*/-100);
    auto sub_low =
        bus.listen<Ping>([&](const Ping&) { log.emplace_back("low"); }, /*priority=*/100);

    bus.publish(conduit::event(Ping{1}).build());

    ASSERT_EQ(log.size(), 3U);
    EXPECT_EQ(log[0], "high");
    EXPECT_EQ(log[1], "default");
    EXPECT_EQ(log[2], "low");
}

class PriorityListener : public conduit::EventListener<Ping> {
public:
    PriorityListener(std::vector<std::string>* log, std::string name, const int p)
        : log_(log), name_(std::move(name)), priority_(p) {}

    void on_event(const Ping&) override {
        log_->push_back(name_);
    }

    [[nodiscard]] int priority() const noexcept override {
        return priority_;
    }

private:
    std::vector<std::string>* log_;
    std::string name_;
    int priority_;
};

TEST(Priority, ClassListenerPriorityFromOverride) {
    std::vector<std::string> log;
    conduit::Bus bus;

    auto a = std::make_shared<PriorityListener>(&log, "A", 10);
    auto b = std::make_shared<PriorityListener>(&log, "B", -5);
    auto c = std::make_shared<PriorityListener>(&log, "C", 0);

    auto sa = bus.listen<Ping>(a);
    auto sb = bus.listen<Ping>(b);
    auto sc = bus.listen<Ping>(c);

    bus.publish(conduit::event(Ping{1}).build());

    ASSERT_EQ(log.size(), 3U);
    EXPECT_EQ(log[0], "B");  // -5
    EXPECT_EQ(log[1], "C");  //  0
    EXPECT_EQ(log[2], "A");  // 10
}

class CountingTransport : public conduit::Transport {
public:
    CountingTransport(std::vector<std::string>* log, std::string name, const int p)
        : log_(log), name_(std::move(name)), priority_(p) {}

    [[nodiscard]] conduit::TransportScope scope() const noexcept override {
        return conduit::TransportScope::Local;
    }

    void dispatch(const conduit::EventEnvelopeView&) override {
        log_->push_back(name_);
    }

    [[nodiscard]] int priority() const noexcept override {
        return priority_;
    }

private:
    std::vector<std::string>* log_;
    std::string name_;
    int priority_;
};

TEST(Priority, TransportsDispatchInPriorityOrder) {
    std::vector<std::string> log;
    conduit::Bus bus;
    bus.use_transport<CountingTransport>(&log, "mid", 0);
    bus.use_transport<CountingTransport>(&log, "high", -10);
    bus.use_transport<CountingTransport>(&log, "low", 10);

    bus.publish(conduit::event(Ping{1}).build());

    ASSERT_EQ(log.size(), 3U);
    EXPECT_EQ(log[0], "high");
    EXPECT_EQ(log[1], "mid");
    EXPECT_EQ(log[2], "low");
}

}  // namespace
