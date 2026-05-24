#include <conduit/fixed_string.hpp>

#include <gtest/gtest.h>

#include <string_view>

namespace {

using conduit::FixedString;

template <FixedString Name>
[[nodiscard]] constexpr std::string_view name_of() noexcept {
    return Name.view();
}

TEST(FixedString, ViewMatchesLiteral) {
    constexpr FixedString fs{"order.created"};
    EXPECT_EQ(fs.view(), std::string_view{"order.created"});
    EXPECT_EQ(fs.size(), 13U);
    EXPECT_FALSE(fs.empty());
}

TEST(FixedString, UsableAsNTTP) {
    EXPECT_EQ(name_of<"foo">(), std::string_view{"foo"});
    EXPECT_EQ(name_of<"order.payment.refunded">(), std::string_view{"order.payment.refunded"});
}

TEST(FixedString, ImplicitlyConvertsToStringView) {
    constexpr FixedString fs{"hello"};
    const std::string_view sv = fs;
    EXPECT_EQ(sv, "hello");
}

TEST(FixedString, EqualityComparesContent) {
    constexpr FixedString a{"abc"};
    constexpr FixedString b{"abc"};
    constexpr FixedString c{"abcd"};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

TEST(FixedString, EmptyDetected) {
    constexpr FixedString fs{""};
    EXPECT_TRUE(fs.empty());
    EXPECT_EQ(fs.size(), 0U);
}

}  // namespace
