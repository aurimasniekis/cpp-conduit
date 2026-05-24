#include <conduit/flags.hpp>

#include <gtest/gtest.h>

namespace {

struct UserFlagA : conduit::flags::Flag<"user.a"> {};
struct UserFlagB : conduit::flags::Flag<"user.b"> {};

TEST(Flags, EmptyByDefault) {
    const conduit::flags::FlagSet s;
    EXPECT_TRUE(s.empty());
    EXPECT_FALSE(s.has<conduit::flags::Direct>());
}

TEST(Flags, SetAndHas) {
    conduit::flags::FlagSet s;
    s.set<conduit::flags::Direct>();
    EXPECT_TRUE(s.has<conduit::flags::Direct>());
    EXPECT_FALSE(s.has<conduit::flags::Durable>());
}

TEST(Flags, OfBuildsFromPack) {
    const auto s = conduit::flags::FlagSet::of<conduit::flags::Direct, conduit::flags::Durable>();
    EXPECT_TRUE(s.has<conduit::flags::Direct>());
    EXPECT_TRUE(s.has<conduit::flags::Durable>());
    EXPECT_FALSE(s.has<conduit::flags::Persistent>());
}

TEST(Flags, UnsetRemoves) {
    auto s = conduit::flags::FlagSet::of<conduit::flags::Direct>();
    s.unset<conduit::flags::Direct>();
    EXPECT_FALSE(s.has<conduit::flags::Direct>());
}

TEST(Flags, OrMerges) {
    const auto a = conduit::flags::FlagSet::of<conduit::flags::Direct>();
    const auto b = conduit::flags::FlagSet::of<conduit::flags::Durable>();
    const auto merged = a | b;
    EXPECT_TRUE(merged.has<conduit::flags::Direct>());
    EXPECT_TRUE(merged.has<conduit::flags::Durable>());
}

TEST(Flags, UserDefinedTagWorks) {
    const auto s = conduit::flags::FlagSet::of<UserFlagA>();
    EXPECT_TRUE(s.has<UserFlagA>());
    EXPECT_FALSE(s.has<UserFlagB>());
}

TEST(Flags, IndexStableAcrossFlagSets) {
    const auto a = conduit::flags::FlagSet::of<UserFlagA, conduit::flags::LocalOnly>();
    const auto b = conduit::flags::FlagSet::of<conduit::flags::LocalOnly, UserFlagA>();
    EXPECT_EQ(a, b);
}

}  // namespace
