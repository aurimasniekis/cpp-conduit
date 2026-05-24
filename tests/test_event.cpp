#include <conduit/event.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include <parcel/parcel.h>

namespace {

struct OrderCreated : conduit::Event<OrderCreated, "order.created"> {
    std::string order_id;
    double total = 0.0;

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<OrderCreated>& b) {
        return b.field<&OrderCreated::order_id>("order_id").field<&OrderCreated::total>("total");
    }
};

struct AppConfigReloadEvent : conduit::Event<AppConfigReloadEvent, "app.config.reload">,
                              conduit::DefaultFlags<conduit::flags::LocalOnly> {
    std::string source_path;

    [[maybe_unused]] static auto&
    event_field_descriptors(parcel::FieldsBuilder<AppConfigReloadEvent>& b) {
        return b.field<&AppConfigReloadEvent::source_path>("source_path");
    }
};

TEST(Event, NameAccessibleAtCompileTime) {
    EXPECT_EQ(OrderCreated::event_name_v, "order.created");
    EXPECT_EQ(AppConfigReloadEvent::event_name_v, "app.config.reload");
}

TEST(Event, KindIdHasConduitPrefix) {
    EXPECT_EQ(OrderCreated::kind_id, "conduit:event:order.created");
    EXPECT_EQ(AppConfigReloadEvent::kind_id, "conduit:event:app.config.reload");
}

TEST(Event, RoundTripsThroughParcel) {
    OrderCreated e;
    e.order_id = "abc";
    e.total = 9.5;

    parcel::ParcelRegistry reg;
    reg.register_kind(OrderCreated::descriptor());

    const auto j = e.to_json();
    auto cell = reg.cell_from_json(j);
    auto restored = parcel::cell_cast<OrderCreated>(cell);
    EXPECT_EQ(restored->order_id, "abc");
    EXPECT_DOUBLE_EQ(restored->total, 9.5);
}

TEST(Event, DefaultFlagsMixinDetected) {
    const auto fs = conduit::detail::collect_default_flags<AppConfigReloadEvent>();
    EXPECT_TRUE(fs.has<conduit::flags::LocalOnly>());

    const auto plain = conduit::detail::collect_default_flags<OrderCreated>();
    EXPECT_TRUE(plain.empty());
}

}  // namespace

// Non-intrusive trait specialization — must live outside the anonymous namespace.
struct ThirdPartyEvent : conduit::Event<ThirdPartyEvent, "third_party.signal"> {
    [[maybe_unused]] static auto&
    event_field_descriptors(parcel::FieldsBuilder<ThirdPartyEvent>& b) {
        return b;
    }
};

template <>
struct conduit::event_traits<ThirdPartyEvent> {
    static flags::FlagSet default_flags() {
        return flags::FlagSet::of<flags::LocalOnly>();
    }
};  // namespace conduit

namespace {

TEST(Event, EventTraitsSpecializationApplies) {
    const auto fs = conduit::detail::collect_default_flags<ThirdPartyEvent>();
    EXPECT_TRUE(fs.has<conduit::flags::LocalOnly>());
}

}  // namespace
