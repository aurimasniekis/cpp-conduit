#include <conduit/glob.hpp>

#include <gtest/gtest.h>

namespace {

TEST(Glob, ExactMatch) {
    EXPECT_TRUE(conduit::Glob::match("order.created", "order.created"));
    EXPECT_FALSE(conduit::Glob::match("order.created", "order.updated"));
}

TEST(Glob, SingleStarMatchesWithinSegment) {
    EXPECT_TRUE(conduit::Glob::match("order.*", "order.created"));
    EXPECT_TRUE(conduit::Glob::match("order.*", "order.updated"));
    EXPECT_FALSE(conduit::Glob::match("order.*", "order.payment.refunded"));
}

TEST(Glob, SingleStarMidPattern) {
    EXPECT_TRUE(conduit::Glob::match("order.*.status", "order.payment.status"));
    EXPECT_TRUE(conduit::Glob::match("order.*.status", "order.shipping.status"));
    EXPECT_FALSE(conduit::Glob::match("order.*.status", "order.payment.late.status"));
    EXPECT_FALSE(conduit::Glob::match("order.*.status", "order.status"));
}

TEST(Glob, DoubleStarCrossesSegments) {
    EXPECT_TRUE(conduit::Glob::match("order.**", "order.created"));
    EXPECT_TRUE(conduit::Glob::match("order.**", "order.payment.refunded"));
    EXPECT_TRUE(conduit::Glob::match("**", "anything.at.all.here"));
}

TEST(Glob, LeadingStar) {
    EXPECT_TRUE(conduit::Glob::match("*.created", "order.created"));
    EXPECT_TRUE(conduit::Glob::match("*.created", "user.created"));
    EXPECT_FALSE(conduit::Glob::match("*.created", "a.b.created"));
    EXPECT_TRUE(conduit::Glob::match("**.created", "a.b.created"));
}

TEST(Glob, EmptyName) {
    EXPECT_TRUE(conduit::Glob::match("", ""));
    EXPECT_FALSE(conduit::Glob::match("", "x"));
    EXPECT_TRUE(conduit::Glob::match("*", ""));
    EXPECT_TRUE(conduit::Glob::match("**", ""));
}

TEST(Glob, ConstructAndPattern) {
    const conduit::Glob g{"order.*"};
    EXPECT_EQ(g.pattern(), "order.*");
    EXPECT_TRUE(g.matches("order.created"));
}

TEST(Glob, OwnedStringStorage) {
    std::string p = "order.*.status";
    const conduit::Glob g{p};
    p.clear();
    EXPECT_TRUE(g.matches("order.payment.status"));
}

}  // namespace
