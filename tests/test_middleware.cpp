#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/flags.hpp>
#include <conduit/local/transport.hpp>
#include <conduit/middleware.hpp>

#include <gtest/gtest.h>

#include <exception>
#include <stdexcept>
#include <vector>

#include <parcel/parcel.h>

namespace {

struct Hi : conduit::Event<Hi, "hi"> {
    int n = 0;
    Hi() = default;
    explicit Hi(const int v) : n(v) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Hi>& b) {
        return b.field<&Hi::n>("n");
    }
};

class RecorderMW : public conduit::Middleware {
public:
    std::vector<std::string>* log;
    explicit RecorderMW(std::vector<std::string>* l, std::string n) : log(l), name(std::move(n)) {}
    bool before_dispatch(conduit::EventEnvelopeView&) override {
        log->push_back(name + ".before");
        return true;
    }
    void after_dispatch(conduit::EventEnvelopeView&) override {
        log->push_back(name + ".after");
    }
    void on_error(conduit::EventEnvelopeView&, const std::exception_ptr&) override {
        log->push_back(name + ".error");
    }
    void on_transport_error(std::string_view transport, const std::exception_ptr&) override {
        log->push_back(name + ".transport_error:" + std::string{transport});
    }
    std::string name;
};

class DropMW : public conduit::Middleware {
public:
    bool before_dispatch(conduit::EventEnvelopeView&) override {
        return false;
    }
};

TEST(Middleware, OrderedBeforeReversedAfter) {
    std::vector<std::string> log;
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    bus.use_middleware<RecorderMW>(&log, "A");
    bus.use_middleware<RecorderMW>(&log, "B");

    bool called = false;
    auto sub = bus.listen<Hi>([&](const Hi&) { called = true; });

    bus.publish(conduit::event(Hi{1}).build());

    EXPECT_TRUE(called);
    ASSERT_EQ(log.size(), 4U);
    EXPECT_EQ(log[0], "A.before");
    EXPECT_EQ(log[1], "B.before");
    EXPECT_EQ(log[2], "B.after");
    EXPECT_EQ(log[3], "A.after");
}

TEST(Middleware, DropShortCircuits) {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    bus.use_middleware<DropMW>();
    bool called = false;
    auto sub = bus.listen<Hi>([&](const Hi&) { called = true; });
    bus.publish(conduit::event(Hi{1}).build());
    EXPECT_FALSE(called);
}

TEST(Middleware, NoMiddlewareFlagSkipsPipeline) {
    std::vector<std::string> log;
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    bus.use_middleware<RecorderMW>(&log, "A");
    auto sub = bus.listen<Hi>([&](const Hi&) {});
    bus.publish(conduit::event(Hi{1}).flag<conduit::flags::NoMiddleware>().build());
    EXPECT_TRUE(log.empty());
}

TEST(Middleware, ListenerExceptionRoutesToOnError) {
    std::vector<std::string> log;
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();
    bus.use_middleware<RecorderMW>(&log, "M");
    auto sub = bus.listen<Hi>([](const Hi&) { throw std::runtime_error("boom"); });

    EXPECT_NO_THROW(bus.publish(conduit::event(Hi{1}).build()));
    bool found_error = false;
    for (const auto& l : log) {
        if (l == "M.error")
            found_error = true;
    }
    EXPECT_TRUE(found_error);
}

TEST(Middleware, ReportTransportErrorFansOutToAllMiddleware) {
    std::vector<std::string> log;
    conduit::Bus bus;
    bus.use_middleware<RecorderMW>(&log, "A");
    bus.use_middleware<RecorderMW>(&log, "B");

    bus.report_transport_error("mqtt",
                               std::make_exception_ptr(std::runtime_error("decode failed")));

    EXPECT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "A.transport_error:mqtt");
    EXPECT_EQ(log[1], "B.transport_error:mqtt");
}

}  // namespace
