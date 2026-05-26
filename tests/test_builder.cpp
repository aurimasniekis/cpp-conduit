#include <conduit/builder.hpp>
#include <conduit/event.hpp>

#include <gtest/gtest.h>

#include <chrono>

#include <parcel/parcel.h>

namespace {

struct Tick : conduit::Event<Tick, "tick"> {
    int n = 0;
    Tick() = default;
    explicit Tick(const int v) : n(v) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Tick>& b) {
        return b.field<&Tick::n>("n");
    }
};

struct LocalOnlyEv : conduit::Event<LocalOnlyEv, "only.local">,
                     conduit::DefaultFlags<conduit::flags::LocalOnly> {
    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<LocalOnlyEv>& b) {
        return b;
    }
};

TEST(Builder, BuildAppliesDefaultIdAndCreatedAt) {
    auto env = conduit::event(Tick{5}).build();
    // Ulid::generate() always returns a non-zero ID since it embeds the current timestamp.
    EXPECT_NE(env.id(), ulid::Ulid{});
    EXPECT_NE(env.timestamps().created_at.time_since_epoch().count(), 0);
}

TEST(Builder, MetadataFluent) {
    auto env =
        conduit::event(Tick{1}).metadata("tenant", "acme").metadata("region", "us-east-1").build();
    EXPECT_EQ(env.metadata().require_string("tenant"), "acme");
    EXPECT_EQ(env.metadata().require_string("region"), "us-east-1");
}

TEST(Builder, FlagFluent) {
    auto env = conduit::event(Tick{1})
                   .flag<conduit::flags::Direct>()
                   .flag<conduit::flags::Durable>()
                   .build();
    EXPECT_TRUE(env.flags().contains<conduit::flags::Direct>());
    EXPECT_TRUE(env.flags().contains<conduit::flags::Durable>());
}

TEST(Builder, DefaultFlagsMixinApplied) {
    auto env = conduit::event(LocalOnlyEv{}).build();
    EXPECT_TRUE(env.flags().contains<conduit::flags::LocalOnly>());
}

TEST(Builder, DefaultFlagsAdditive) {
    auto env = conduit::event(LocalOnlyEv{}).flag<conduit::flags::Durable>().build();
    EXPECT_TRUE(env.flags().contains<conduit::flags::LocalOnly>());
    EXPECT_TRUE(env.flags().contains<conduit::flags::Durable>());
}

TEST(Builder, ImplicitConversionToEnvelope) {
    conduit::EventEnvelope env = conduit::event(Tick{3}).flag<conduit::flags::Direct>();
    EXPECT_TRUE(env.flags().contains<conduit::flags::Direct>());
    const auto p = env.payload_as<Tick>();
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(p->n, 3);
}

TEST(Builder, MakeEventForwards) {
    const auto env = conduit::make_event<Tick>(11).build();
    const auto p = env.payload_as<Tick>();
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(p->n, 11);
}

TEST(Builder, IdOverride) {
    const auto explicit_id = ulid::generate();
    const auto env = conduit::event(Tick{1}).id(explicit_id).build();
    EXPECT_EQ(env.id(), explicit_id);
}

TEST(Builder, CorrelationAndCausation) {
    const auto cor = ulid::generate();
    const auto cau = ulid::generate();
    const auto env = conduit::event(Tick{1}).correlation_id(cor).causation_id(cau).build();
    ASSERT_TRUE(env.correlation_id().has_value());
    EXPECT_EQ(*env.correlation_id(), cor);
    ASSERT_TRUE(env.causation_id().has_value());
    EXPECT_EQ(*env.causation_id(), cau);
}

}  // namespace
