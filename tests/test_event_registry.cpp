#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/serialization.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>

#include <parcel/parcel.h>

namespace {

struct A : conduit::Event<A, "registry.a"> {
    int x = 0;
    A() = default;
    explicit A(const int v) : x(v) {}
    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<A>& b) {
        return b.field<&A::x>("x");
    }
};

struct B : conduit::Event<B, "registry.b"> {
    int x = 0;
    B() = default;
    explicit B(const int v) : x(v) {}
    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<B>& b) {
        return b.field<&B::x>("x");
    }
};

TEST(EventRegistry, AddThenDecodeRoundtripCbor) {
    conduit::EventRegistry reg;
    reg.add<A>();

    auto env = conduit::event(A{42}).build();
    auto bytes = conduit::encode_cbor(env);
    auto decoded = reg.decode_cbor(bytes);
    ASSERT_TRUE(decoded.valid());
    EXPECT_EQ(decoded.name(), "registry.a");
    auto p = decoded.payload_as<A>();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->x, 42);
}

TEST(EventRegistry, AddDescriptorThreadSafeAgainstDecode) {
    const auto reg = std::make_shared<conduit::EventRegistry>();
    reg->add<A>();

    const auto env = conduit::event(A{7}).build();
    const auto bytes = conduit::encode_cbor(env);

    std::atomic<bool> stop{false};
    std::atomic<int> decode_hits{0};
    std::atomic<bool> failure{false};

    std::thread writer([&]() {
        // Spam the registry with new descriptor registrations. Re-registering
        // an already-known kind must be safe and must not invalidate readers.
        for (int i = 0; i < 5000 && !stop.load(std::memory_order_acquire); ++i) {
            reg->add<B>();
            reg->add<A>();
        }
    });

    std::thread reader([&]() {
        for (int i = 0; i < 5000; ++i) {
            try {
                if (auto decoded = reg->decode_cbor(bytes); !decoded.valid()) {
                    failure.store(true);
                }
                decode_hits.fetch_add(1);
            } catch (...) {
                failure.store(true);
                break;
            }
        }
    });

    reader.join();
    stop.store(true, std::memory_order_release);
    writer.join();

    EXPECT_FALSE(failure.load());
    EXPECT_GT(decode_hits.load(), 0);
}

TEST(EventRegistry, BusOwnsDefaultRegistry) {
    conduit::Bus bus;
    auto reg = bus.registry();
    ASSERT_NE(reg, nullptr);

    bus.register_event<A>();
    auto env = conduit::event(A{1}).build();
    auto bytes = conduit::encode_cbor(env);
    auto decoded = reg->decode_cbor(bytes);
    EXPECT_TRUE(decoded.valid());
}

TEST(EventRegistry, BusUsesCallerSuppliedRegistry) {
    const auto reg = std::make_shared<conduit::EventRegistry>();
    reg->add<A>();
    const conduit::Bus bus{reg};
    EXPECT_EQ(bus.registry().get(), reg.get());
}

}  // namespace
