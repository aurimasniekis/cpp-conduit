#include <conduit/bus.hpp>
#include <conduit/event.hpp>
#include <conduit/event_type_registry.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include <parcel/parcel.h>

namespace {

// Macro-registered at namespace scope (below). Carries display info so we can
// assert it survives the round-trip through the descriptor.
struct CatalogOrder : conduit::Event<CatalogOrder, "catalog.order"> {
    std::string order_id;
    double total = 0.0;

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<CatalogOrder>& b) {
        return b.field<&CatalogOrder::order_id>("order_id").field<&CatalogOrder::total>("total");
    }

    static parcel::DisplayInfo display_info() {
        return {.name = "Catalog Order", .description = "An order placed in the catalog."};
    }
};

// Registered only via a local standalone registry / never macro-registered.
struct StandaloneOnly : conduit::Event<StandaloneOnly, "catalog.standalone"> {
    int n = 0;

    [[maybe_unused]] static auto&
    event_field_descriptors(parcel::FieldsBuilder<StandaloneOnly>& b) {
        return b.field<&StandaloneOnly::n>("n");
    }
};

// Used by the decoupling test: listened-for on a Bus but never registered with
// the type catalog.
struct BusOnlyEvent : conduit::Event<BusOnlyEvent, "catalog.bus_only"> {
    int n = 0;

    [[maybe_unused]] static auto& event_field_descriptors(parcel::FieldsBuilder<BusOnlyEvent>& b) {
        return b.field<&BusOnlyEvent::n>("n");
    }
};

}  // namespace

CONDUIT_REGISTER_EVENT(CatalogOrder);

namespace {

TEST(EventTypeRegistry, MacroRegistersIntoGlobalCatalog) {
    auto& reg = conduit::global_event_types();

    EXPECT_TRUE(reg.contains("catalog.order"));
    EXPECT_TRUE(reg.contains("conduit:event:catalog.order"));

    const auto info = reg.find("catalog.order");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->name, "catalog.order");
    EXPECT_EQ(info->kind_id, "conduit:event:catalog.order");
    ASSERT_NE(info->descriptor, nullptr);

    const auto di = info->display_info();
    ASSERT_TRUE(di.name.has_value());
    EXPECT_EQ(*di.name, "Catalog Order");

    // Lookup by full kind yields the same identity.
    const auto by_kind = reg.find("conduit:event:catalog.order");
    ASSERT_TRUE(by_kind.has_value());
    EXPECT_EQ(by_kind->kind_id, "conduit:event:catalog.order");
}

TEST(EventTypeRegistry, SchemaExposesStructFields) {
    auto& reg = conduit::global_event_types();

    const auto info = reg.find("catalog.order");
    ASSERT_TRUE(info.has_value());

    // schema() on the info and on the registry agree, and equal the
    // descriptor's own to_json().
    const auto schema = reg.schema("catalog.order");
    EXPECT_EQ(schema, info->schema());
    EXPECT_EQ(schema, info->descriptor->to_json());

    ASSERT_TRUE(schema.contains("category"));
    EXPECT_EQ(schema.at("category").get<std::string>(), "struct");

    ASSERT_TRUE(schema.contains("fields"));
    ASSERT_TRUE(schema.at("fields").is_array());

    bool saw_order_id = false;
    bool saw_total = false;
    for (const auto& f : schema.at("fields")) {
        ASSERT_TRUE(f.contains("key"));
        ASSERT_TRUE(f.contains("kind"));
        if (const auto key = f.at("key").get<std::string>(); key == "order_id") {
            saw_order_id = true;
            EXPECT_FALSE(f.at("kind").get<std::string>().empty());
        } else if (key == "total") {
            saw_total = true;
            EXPECT_FALSE(f.at("kind").get<std::string>().empty());
        }
    }
    EXPECT_TRUE(saw_order_id);
    EXPECT_TRUE(saw_total);
}

TEST(EventTypeRegistry, SchemaThrowsForUnknownType) {
    EXPECT_THROW((void)conduit::global_event_types().schema("does.not.exist"), std::out_of_range);
}

TEST(EventTypeRegistry, RegisteredTypesListsEventAndExcludesBuiltins) {
    const auto types = conduit::registered_event_types();

    const bool has_order = std::ranges::any_of(
        types, [](const conduit::EventTypeInfo& i) { return i.name == "catalog.order"; });
    EXPECT_TRUE(has_order);

    // Every entry is an event kind — parcel builtins (flag_set, primitives, …)
    // are filtered out.
    for (const auto& i : types) {
        EXPECT_TRUE(i.kind_id.starts_with(conduit::event_kind_prefix));
        EXPECT_NE(i.name, "flag_set");
    }
}

TEST(EventTypeRegistry, StandaloneInstanceIsIndependentOfGlobal) {
    conduit::EventTypeRegistry local;
    EXPECT_FALSE(local.contains("catalog.standalone"));

    local.add<StandaloneOnly>();
    EXPECT_TRUE(local.contains("catalog.standalone"));

    const auto types = local.types();
    const bool has_standalone = std::ranges::any_of(
        types, [](const conduit::EventTypeInfo& i) { return i.name == "catalog.standalone"; });
    EXPECT_TRUE(has_standalone);

    // The local registry does not leak into the global catalog, and vice versa.
    EXPECT_FALSE(conduit::global_event_types().contains("catalog.standalone"));
    EXPECT_FALSE(local.contains("catalog.order"));
}

TEST(EventTypeRegistry, BusDoesNotFeedTypeCatalog) {
    ASSERT_FALSE(conduit::global_event_types().contains("catalog.bus_only"));

    conduit::Bus bus;
    const auto sub = bus.listen<BusOnlyEvent>([](const conduit::EventEnvelope&) {});
    (void)sub;

    // Listening / registering on the Bus must not populate the type catalog.
    EXPECT_FALSE(conduit::global_event_types().contains("catalog.bus_only"));
}

}  // namespace
