#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/flags.hpp>
#include <conduit/local/transport.hpp>
#include <conduit/middleware.hpp>
#include <conduit/transport.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <exception>

#include <parcel/parcel.h>

namespace {

struct Plain : conduit::Event<Plain, "plain"> {
    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Plain>& b) {
        return b;
    }
};

struct LocalEv : conduit::Event<LocalEv, "local.only">,
                 conduit::DefaultFlags<conduit::flags::LocalOnly> {
    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<LocalEv>& b) {
        return b;
    }
};

struct RemoteEv : conduit::Event<RemoteEv, "remote.only">,
                  conduit::DefaultFlags<conduit::flags::RemoteOnly> {
    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<RemoteEv>& b) {
        return b;
    }
};

class FakeLocalT : public conduit::Transport {
public:
    std::atomic<int> count{0};
    conduit::TransportScope scope() const noexcept override {
        return conduit::TransportScope::Local;
    }
    void dispatch(const conduit::EventEnvelopeView&) override {
        ++count;
    }
};

class FakeRemoteT : public conduit::Transport {
public:
    std::atomic<int> count{0};
    conduit::TransportScope scope() const noexcept override {
        return conduit::TransportScope::Remote;
    }
    void dispatch(const conduit::EventEnvelopeView&) override {
        ++count;
    }
};

class ErrorCountingMW : public conduit::Middleware {
public:
    std::atomic<int> errors{0};
    void on_error(conduit::EventEnvelopeView&, const std::exception_ptr&) override {
        ++errors;
    }
};

TEST(Scope, LocalOnlySkipsRemoteTransport) {
    conduit::Bus bus;
    const auto& local = bus.use_transport<FakeLocalT>();
    const auto& remote = bus.use_transport<FakeRemoteT>();

    bus.publish(conduit::event(LocalEv{}).build());
    EXPECT_EQ(local.count, 1);
    EXPECT_EQ(remote.count, 0);
}

TEST(Scope, RemoteOnlySkipsLocalTransport) {
    conduit::Bus bus;
    const auto& local = bus.use_transport<FakeLocalT>();
    const auto& remote = bus.use_transport<FakeRemoteT>();

    bus.publish(conduit::event(RemoteEv{}).build());
    EXPECT_EQ(local.count, 0);
    EXPECT_EQ(remote.count, 1);
}

TEST(Scope, NoFlagsRoutesToAllTransports) {
    conduit::Bus bus;
    const auto& local = bus.use_transport<FakeLocalT>();
    const auto& remote = bus.use_transport<FakeRemoteT>();
    bus.publish(conduit::event(Plain{}).build());
    EXPECT_EQ(local.count, 1);
    EXPECT_EQ(remote.count, 1);
}

TEST(Scope, ConflictRoutesToOnError) {
    conduit::Bus bus;
    const auto& local = bus.use_transport<FakeLocalT>();
    const auto& remote = bus.use_transport<FakeRemoteT>();
    const auto& mw = bus.use_middleware<ErrorCountingMW>();
    bus.publish(conduit::event(Plain{})
                    .flag<conduit::flags::LocalOnly>()
                    .flag<conduit::flags::RemoteOnly>()
                    .build());
    EXPECT_EQ(local.count, 0);
    EXPECT_EQ(remote.count, 0);
    EXPECT_GE(mw.errors, 1);
}

TEST(Scope, DefaultFlagsMixinAppliedAutomatically) {
    auto env = conduit::event(LocalEv{}).build();
    EXPECT_TRUE(env.flags().has<conduit::flags::LocalOnly>());

    auto env2 = conduit::event(RemoteEv{}).build();
    EXPECT_TRUE(env2.flags().has<conduit::flags::RemoteOnly>());
}

// event_traits<T> specialization for a foreign event.
struct Foreign : conduit::Event<Foreign, "foreign.signal"> {
    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Foreign>& b) {
        return b;
    }
};

}  // namespace

template <>
struct conduit::event_traits<Foreign> {
    static flags::FlagSet default_flags() {
        return flags::FlagSet::of<flags::LocalOnly>();
    }
};  // namespace conduit

namespace {

TEST(Scope, EventTraitsSpecializationSkipsRemote) {
    conduit::Bus bus;
    const auto& local = bus.use_transport<FakeLocalT>();
    const auto& remote = bus.use_transport<FakeRemoteT>();
    bus.publish(conduit::event(Foreign{}).build());
    EXPECT_EQ(local.count, 1);
    EXPECT_EQ(remote.count, 0);
}

}  // namespace
