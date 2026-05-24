#include <conduit/builder.hpp>
#include <conduit/event.hpp>
#include <conduit/serialization.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <string>

#include <parcel/parcel.h>

namespace {

struct Pong : conduit::Event<Pong, "pong"> {
    std::string text;
    Pong() = default;
    explicit Pong(std::string t) : text(std::move(t)) {}

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<Pong>& b) {
        return b.field<&Pong::text>("text");
    }
};

TEST(SerializationCbor, RoundTrips) {
    auto env =
        conduit::event(Pong{"hi"}).metadata("k", "v").flag<conduit::flags::Durable>().build();

    auto bytes = conduit::serialization::encode_cbor(env);
    EXPECT_GT(bytes.size(), 0U);

    conduit::serialization::EventRegistry reg;
    reg.add<Pong>();

    auto v = reg.decode_cbor(std::span<const std::uint8_t>{bytes});
    auto recovered = v.payload_as<Pong>();
    ASSERT_TRUE(recovered != nullptr);
    EXPECT_EQ(recovered->text, "hi");
    EXPECT_EQ(v.metadata().require_string("k"), "v");
    EXPECT_TRUE(v.flags().has<conduit::flags::Durable>());
}

TEST(SerializationCbor, CborIsSmallerThanJsonForBinaryPayload) {
    const auto env = conduit::event(Pong{std::string(256, 'a')}).build();
    const auto j = conduit::serialization::encode_json(env);
    const auto bytes = conduit::serialization::encode_cbor(env);
    EXPECT_LE(bytes.size(), j.dump().size());
}

}  // namespace
