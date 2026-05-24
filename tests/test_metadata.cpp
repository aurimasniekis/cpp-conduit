#include <conduit/metadata.hpp>

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {

TEST(Metadata, InsertAndLookup) {
    conduit::Metadata md;
    md["tenant"] = "acme";
    md["region"] = "eu-west-1";
    EXPECT_EQ(md.require_string("tenant"), "acme");
    EXPECT_EQ(md.require_string("region"), "eu-west-1");
}

TEST(Metadata, HeterogeneousFindByStringView) {
    conduit::Metadata md{{"k", "v"}};
    const auto it = md.find(std::string_view{"k"});
    ASSERT_NE(it, md.end());
    EXPECT_EQ(it->second.as_string(), "v");
}

TEST(Metadata, GetStringIfMissingReturnsNull) {
    const conduit::Metadata md{{"a", "1"}};
    ASSERT_NE(md.get_string_if("a"), nullptr);
    EXPECT_EQ(*md.get_string_if("a"), "1");
    EXPECT_EQ(md.get_string_if("missing"), nullptr);
}

TEST(Metadata, RichValueTypes) {
    conduit::Metadata md;
    md["count"] = 42;       // int64
    md["ratio"] = 0.5;      // double
    md["enabled"] = true;   // bool
    md["name"] = "sensor";  // string
    EXPECT_EQ(md.at("count").as_int(), 42);
    EXPECT_DOUBLE_EQ(md.at("ratio").as_double(), 0.5);
    EXPECT_TRUE(md.at("enabled").as_bool());
    EXPECT_EQ(md.at("name").as_string(), "sensor");
}

TEST(Timestamps, DefaultsAreNullopt) {
    constexpr conduit::Timestamps ts;
    EXPECT_FALSE(ts.published_at.has_value());
    EXPECT_FALSE(ts.received_at.has_value());
    EXPECT_FALSE(ts.delivered_at.has_value());
    EXPECT_FALSE(ts.failed_at.has_value());
}

}  // namespace
