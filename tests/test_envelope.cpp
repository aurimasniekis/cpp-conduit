#include <conduit/builder.hpp>
#include <conduit/envelope.hpp>
#include <conduit/event.hpp>
#include <conduit/flags.hpp>
#include <conduit/serialization.hpp>

#include <gtest/gtest.h>
#include <ulid/ulid.h>

#include <chrono>
#include <cstdint>
#include <span>
#include <string>

#include <parcel/parcel.h>

namespace {

struct PingEvent : conduit::Event<PingEvent, "ping"> {
    int n = 0;

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<PingEvent>& b) {
        return b.field<&PingEvent::n>("n");
    }
};

struct PongEvent : conduit::Event<PongEvent, "pong"> {
    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<PongEvent>& b) {
        return b;
    }
};

TEST(Envelope, CarriesPayload) {
    PingEvent p;
    p.n = 42;
    const conduit::EventEnvelope env{p};
    const auto recovered = env.payload_as<PingEvent>();
    ASSERT_TRUE(recovered != nullptr);
    EXPECT_EQ(recovered->n, 42);
    EXPECT_EQ(env.name(), "ping");
}

TEST(Envelope, MetadataMutable) {
    conduit::EventEnvelope env{PingEvent{}};
    env.metadata().insert_or_assign("k", md::Value{"v"});
    EXPECT_EQ(env.metadata().require_string("k"), "v");
}

TEST(Envelope, ValidWhenPayloadPresent) {
    PingEvent p;
    p.n = 7;
    const conduit::EventEnvelope env{p};
    EXPECT_TRUE(env.valid());
    EXPECT_EQ(env.name(), "ping");
}

TEST(Envelope, PayloadAsRecoversTypedPayload) {
    PingEvent p;
    p.n = 5;
    const conduit::EventEnvelope env{p};

    const auto recovered = env.payload_as<PingEvent>();
    ASSERT_TRUE(recovered != nullptr);
    EXPECT_EQ(recovered->n, 5);

    const auto wrong = env.payload_as<PongEvent>();
    EXPECT_EQ(wrong, nullptr);
}

TEST(Envelope, CopiesShareCore) {
    conduit::EventEnvelope env{PingEvent{}};
    conduit::EventEnvelope copy = env;
    copy.metadata().insert_or_assign("transport", md::Value{"test"});
    EXPECT_EQ(env.metadata().require_string("transport"), "test");
}

// ---------------------------------------------------------------------------
// Full encode/decode round-trips against the parcel-backed registry.
// These exercise the envelope wire path independently of the bus.
// ---------------------------------------------------------------------------

struct TwoField : conduit::Event<TwoField, "envelope.test.two_field"> {
    std::string label;
    double score = 0.0;
    TwoField() = default;
    TwoField(std::string l, const double s) : label(std::move(l)), score(s) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<TwoField>& b) {
        return b.field<&TwoField::label>("label").field<&TwoField::score>("score");
    }
};

conduit::EventEnvelope make_fully_populated_envelope() {
    return conduit::event(TwoField{"alpha", 1.25})
        .metadata("tenant", "acme")
        .metadata("region", "eu-west")
        .flag<conduit::flags::Durable>()
        .flag<conduit::flags::RequireAck>()
        .correlation_id(ulid::generate())
        .causation_id(ulid::generate())
        .build();
}

TEST(EnvelopeWire, JsonRoundTripPreservesEveryField) {
    auto env = make_fully_populated_envelope();
    // Stamp every optional timestamp slot so we exercise the full wire shape.
    const auto now = std::chrono::system_clock::now();
    env.timestamps().published_at = now + std::chrono::milliseconds{1};
    env.timestamps().received_at = now + std::chrono::milliseconds{2};
    env.timestamps().delivered_at = now + std::chrono::milliseconds{3};
    env.timestamps().failed_at = now + std::chrono::milliseconds{4};

    const auto original_id = env.id();
    const auto original_cor = *env.correlation_id();
    const auto original_cau = *env.causation_id();

    conduit::serialization::EventRegistry reg;
    reg.add<TwoField>();

    const auto j = conduit::serialization::encode_json(env);
    EXPECT_EQ(j.at("k").get<std::string>(), "conduit:envelope");

    const auto decoded = reg.decode_json(j);

    EXPECT_EQ(decoded.name(), "envelope.test.two_field");
    EXPECT_EQ(decoded.id(), original_id);
    ASSERT_TRUE(decoded.correlation_id().has_value());
    EXPECT_EQ(*decoded.correlation_id(), original_cor);
    ASSERT_TRUE(decoded.causation_id().has_value());
    EXPECT_EQ(*decoded.causation_id(), original_cau);

    EXPECT_EQ(decoded.metadata().require_string("tenant"), "acme");
    EXPECT_EQ(decoded.metadata().require_string("region"), "eu-west");
    EXPECT_TRUE(decoded.flags().has<conduit::flags::Durable>());
    EXPECT_TRUE(decoded.flags().has<conduit::flags::RequireAck>());

    // Timestamps are encoded at millisecond resolution — compare at that grain.
    using msec = std::chrono::milliseconds;
    const auto src = env.timestamps();
    const auto dst = decoded.timestamps();
    EXPECT_EQ(std::chrono::duration_cast<msec>(dst.created_at.time_since_epoch()),
              std::chrono::duration_cast<msec>(src.created_at.time_since_epoch()));
    ASSERT_TRUE(dst.published_at.has_value());
    ASSERT_TRUE(dst.received_at.has_value());
    ASSERT_TRUE(dst.delivered_at.has_value());
    ASSERT_TRUE(dst.failed_at.has_value());
    EXPECT_EQ(std::chrono::duration_cast<msec>(dst.published_at->time_since_epoch()),
              std::chrono::duration_cast<msec>(src.published_at->time_since_epoch()));
    EXPECT_EQ(std::chrono::duration_cast<msec>(dst.received_at->time_since_epoch()),
              std::chrono::duration_cast<msec>(src.received_at->time_since_epoch()));
    EXPECT_EQ(std::chrono::duration_cast<msec>(dst.delivered_at->time_since_epoch()),
              std::chrono::duration_cast<msec>(src.delivered_at->time_since_epoch()));
    EXPECT_EQ(std::chrono::duration_cast<msec>(dst.failed_at->time_since_epoch()),
              std::chrono::duration_cast<msec>(src.failed_at->time_since_epoch()));

    auto payload = decoded.payload_as<TwoField>();
    ASSERT_TRUE(payload != nullptr);
    EXPECT_EQ(payload->label, "alpha");
    EXPECT_DOUBLE_EQ(payload->score, 1.25);
}

TEST(EnvelopeWire, CborRoundTripPreservesEveryField) {
    auto env = make_fully_populated_envelope();

    conduit::serialization::EventRegistry reg;
    reg.add<TwoField>();

    const auto bytes = conduit::serialization::encode_cbor(env);
    EXPECT_GT(bytes.size(), 0U);

    const auto decoded = reg.decode_cbor(std::span<const std::uint8_t>{bytes});

    EXPECT_EQ(decoded.id(), env.id());
    EXPECT_EQ(decoded.metadata().require_string("tenant"), "acme");
    EXPECT_TRUE(decoded.flags().has<conduit::flags::Durable>());

    auto payload = decoded.payload_as<TwoField>();
    ASSERT_TRUE(payload != nullptr);
    EXPECT_EQ(payload->label, "alpha");
    EXPECT_DOUBLE_EQ(payload->score, 1.25);
}

TEST(EnvelopeWire, JsonAndCborEncodeToTheSameLogicalDocument) {
    const auto env = make_fully_populated_envelope();
    const auto j = conduit::serialization::encode_json(env);
    const auto bytes = conduit::serialization::encode_cbor(env);

    // CBOR is JSON-equivalent through nlohmann's CBOR codec.
    const auto rehydrated = parcel::json_t::from_cbor(bytes);
    EXPECT_EQ(rehydrated, j);
}

TEST(EnvelopeWire, OneRegistryDispatchesMultipleEventTypes) {
    conduit::serialization::EventRegistry reg;
    reg.add<PingEvent>();
    reg.add<TwoField>();

    PingEvent ping_payload;
    ping_payload.n = 11;
    auto ping_env = conduit::event(ping_payload).build();
    auto two_env = conduit::event(TwoField{"x", 2.5}).build();

    const auto ping_j = conduit::serialization::encode_json(ping_env);
    const auto two_j = conduit::serialization::encode_json(two_env);

    auto ping_back = reg.decode_json(ping_j);
    auto two_back = reg.decode_json(two_j);

    EXPECT_EQ(ping_back.name(), "ping");
    EXPECT_EQ(two_back.name(), "envelope.test.two_field");
    ASSERT_NE(ping_back.payload_as<PingEvent>(), nullptr);
    ASSERT_NE(two_back.payload_as<TwoField>(), nullptr);
    EXPECT_EQ(ping_back.payload_as<PingEvent>()->n, 11);
    EXPECT_EQ(two_back.payload_as<TwoField>()->label, "x");
    EXPECT_DOUBLE_EQ(two_back.payload_as<TwoField>()->score, 2.5);

    // Cross-type payload_as must return nullptr.
    EXPECT_EQ(ping_back.payload_as<TwoField>(), nullptr);
    EXPECT_EQ(two_back.payload_as<PingEvent>(), nullptr);
}

TEST(EnvelopeWire, MissingPayloadKindThrows) {
    auto env = conduit::event(TwoField{"x", 0.0}).build();
    const auto j = conduit::serialization::encode_json(env);

    conduit::serialization::EventRegistry reg;  // TwoField intentionally not registered
    EXPECT_THROW({ (void)reg.decode_json(j); }, conduit::SerializationError);
}

TEST(EnvelopeWire, EmptyEnvelopeRefusesToEncode) {
    conduit::EventEnvelope empty;  // no payload cell attached
    EXPECT_FALSE(empty.valid());
    EXPECT_THROW({ (void)empty.to_json(); }, std::exception);
}

}  // namespace
