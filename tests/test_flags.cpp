#include <conduit/flags.hpp>

#include <gtest/gtest.h>

#include <commons/color.hpp>
#include <commons/display_info.hpp>

namespace {

struct UserFlagA : conduit::flags::Flag<"user.a"> {};
struct UserFlagB : conduit::flags::Flag<"user.b"> {};

TEST(Flags, EmptyByDefault) {
    const conduit::flags::FlagSet s;
    EXPECT_TRUE(s.empty());
    EXPECT_FALSE(s.contains<conduit::flags::Direct>());
}

TEST(Flags, InsertAndContains) {
    conduit::flags::FlagSet s;
    s.insert<conduit::flags::Direct>();
    EXPECT_TRUE(s.contains<conduit::flags::Direct>());
    EXPECT_FALSE(s.contains<conduit::flags::Durable>());
}

TEST(Flags, OfBuildsFromPack) {
    const auto s = conduit::flags::FlagSet::of<conduit::flags::Direct, conduit::flags::Durable>();
    EXPECT_TRUE(s.contains<conduit::flags::Direct>());
    EXPECT_TRUE(s.contains<conduit::flags::Durable>());
    EXPECT_FALSE(s.contains<conduit::flags::Persistent>());
}

TEST(Flags, EraseRemoves) {
    auto s = conduit::flags::FlagSet::of<conduit::flags::Direct>();
    s.erase<conduit::flags::Direct>();
    EXPECT_FALSE(s.contains<conduit::flags::Direct>());
}

TEST(Flags, UserDefinedTagWorks) {
    const auto s = conduit::flags::FlagSet::of<UserFlagA>();
    EXPECT_TRUE(s.contains<UserFlagA>());
    EXPECT_FALSE(s.contains<UserFlagB>());
}

TEST(Flags, BuiltInsLandInConduitCategory) {
    EXPECT_EQ(conduit::flags::Direct::category_name, "conduit");
    EXPECT_EQ(conduit::flags::Broadcast::category_name, "conduit");
    EXPECT_EQ(conduit::flags::Direct::category::name, conduit::flags::ConduitFlagCategory::name);
}

TEST(Flags, UserFlagDefaultsToConduitCategory) {
    EXPECT_EQ(UserFlagA::category_name, "conduit");
}

TEST(Flags, BuiltInsCarryDisplayInfo) {
    static_assert(comms::Displayable<conduit::flags::Direct>);
    static_assert(comms::Displayable<conduit::flags::Durable>);
    static_assert(comms::Displayable<conduit::flags::Persistent>);
    static_assert(comms::Displayable<conduit::flags::NoMiddleware>);
    static_assert(comms::Displayable<conduit::flags::RequireAck>);
    static_assert(comms::Displayable<conduit::flags::Broadcast>);
    static_assert(comms::Displayable<conduit::flags::LocalOnly>);
    static_assert(comms::Displayable<conduit::flags::RemoteOnly>);
}

TEST(Flags, DisplayInfoColorsMatchMuiPalette) {
    EXPECT_EQ(comms::display_info<conduit::flags::Direct>().color, comms::Colors::mui::yellow_700);
    EXPECT_EQ(comms::display_info<conduit::flags::Durable>().color, comms::Colors::mui::blue_700);
    EXPECT_EQ(comms::display_info<conduit::flags::Persistent>().color,
              comms::Colors::mui::teal_500);
    EXPECT_EQ(comms::display_info<conduit::flags::NoMiddleware>().color,
              comms::Colors::mui::grey_600);
    EXPECT_EQ(comms::display_info<conduit::flags::RequireAck>().color,
              comms::Colors::mui::green_600);
    EXPECT_EQ(comms::display_info<conduit::flags::Broadcast>().color,
              comms::Colors::mui::deep_orange_500);
    EXPECT_EQ(comms::display_info<conduit::flags::LocalOnly>().color,
              comms::Colors::mui::light_blue_500);
    EXPECT_EQ(comms::display_info<conduit::flags::RemoteOnly>().color,
              comms::Colors::mui::indigo_500);
}

TEST(Flags, BuiltInsRegisteredGlobally) {
    const auto& reg = comms::GlobalFlagRegistry::instance();
    EXPECT_TRUE(reg.find("direct").has_value());
    EXPECT_TRUE(reg.find("durable").has_value());
    EXPECT_TRUE(reg.find("require_ack").has_value());
    EXPECT_TRUE(reg.find("broadcast").has_value());
    EXPECT_FALSE(reg.find("does_not_exist").has_value());
}

TEST(Flags, AllBuiltInsRegisteredGlobally) {
    const auto& reg = comms::GlobalFlagRegistry::instance();

    // Keyed off each flag's own `name`, so the assertion can't drift from the
    // literal passed to `Flag<"...">`. Every built-in must resolve and land in
    // the conduit category.
    const auto check = [&reg]<typename F>() {
        const auto ref = reg.find(F::name);
        EXPECT_TRUE(ref.has_value()) << "built-in flag not registered globally: " << F::name;
        if (ref.has_value()) {
            EXPECT_EQ(ref->name, F::name);
            EXPECT_EQ(ref->category, conduit::flags::ConduitFlagCategory::name);
        }
    };

    check.operator()<conduit::flags::Direct>();
    check.operator()<conduit::flags::Durable>();
    check.operator()<conduit::flags::Persistent>();
    check.operator()<conduit::flags::NoMiddleware>();
    check.operator()<conduit::flags::RequireAck>();
    check.operator()<conduit::flags::Broadcast>();
    check.operator()<conduit::flags::LocalOnly>();
    check.operator()<conduit::flags::RemoteOnly>();
}

}  // namespace
