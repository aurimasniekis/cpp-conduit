#include <conduit/builder.hpp>
#include <conduit/event.hpp>
#include <conduit/serialization.hpp>

#include <gtest/gtest.h>

#include <string>

#include <parcel/parcel.h>

namespace {

struct Ping : conduit::Event<Ping, "ping"> {
    int n = 0;
    Ping() = default;
    explicit Ping(const int v) : n(v) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Ping>& b) {
        return b.field<&Ping::n>("n");
    }
};

TEST(SerializationJson, EncodeIncludesAllFields) {
    auto env = conduit::event(Ping{7}).metadata("k", "v").flag<conduit::flags::Direct>().build();
    auto j = conduit::serialization::encode_json(env);

    EXPECT_EQ(j.at("k").get<std::string>(), "conduit:envelope");
    const auto& v = j.at("v");
    EXPECT_EQ(v.at("name").get<std::string>(), "ping");
    EXPECT_TRUE(v.contains("id"));
    EXPECT_TRUE(v.contains("timestamps"));
    EXPECT_TRUE(v.contains("metadata"));

    // Payload is itself a wrapped cell.
    const auto& payload = v.at("payload");
    EXPECT_EQ(payload.at("k").get<std::string>(), "conduit:event:ping");
    EXPECT_EQ(payload.at("v").at("n").at("v").get<int>(), 7);
}

TEST(SerializationJson, RoundTripPreservesPayload) {
    auto env = conduit::event(Ping{17}).build();
    auto j = conduit::serialization::encode_json(env);

    conduit::serialization::EventRegistry reg;
    reg.add<Ping>();

    auto v = reg.decode_json(j);
    auto recovered = v.payload_as<Ping>();
    ASSERT_TRUE(recovered != nullptr);
    EXPECT_EQ(recovered->n, 17);
}

TEST(SerializationJson, RoundTripPreservesIdAndMetadata) {
    auto env = conduit::event(Ping{1}).metadata("a", "1").metadata("b", "2").build();
    const auto original_id = env.id();
    auto j = conduit::serialization::encode_json(env);

    conduit::serialization::EventRegistry reg;
    reg.add<Ping>();
    auto v = reg.decode_json(j);
    EXPECT_EQ(v.id(), original_id);
    EXPECT_EQ(v.metadata().require_string("a"), "1");
    EXPECT_EQ(v.metadata().require_string("b"), "2");
}

TEST(SerializationJson, RoundTripPreservesFlags) {
    auto env = conduit::event(Ping{1})
                   .flag<conduit::flags::Direct>()
                   .flag<conduit::flags::LocalOnly>()
                   .build();
    auto j = conduit::serialization::encode_json(env);
    conduit::serialization::EventRegistry reg;
    reg.add<Ping>();
    auto v = reg.decode_json(j);
    EXPECT_TRUE(v.flags().has<conduit::flags::Direct>());
    EXPECT_TRUE(v.flags().has<conduit::flags::LocalOnly>());
}

TEST(SerializationJson, UnknownNameThrows) {
    auto env = conduit::event(Ping{1}).build();
    auto j = conduit::serialization::encode_json(env);
    conduit::serialization::EventRegistry reg;  // intentionally empty (no Ping)
    EXPECT_THROW({ (void)reg.decode_json(j); }, conduit::SerializationError);
}

}  // namespace
