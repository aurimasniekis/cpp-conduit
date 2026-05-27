#pragma once

/// @file
/// @brief Process-wide catalog of conduit event *types* — name, shape, and
///        display info per type, plus a JSON schema derived from the underlying
///        parcel descriptor.
///
/// This is an *introspection* facility, distinct from the per-`Bus`
/// `conduit::EventRegistry` (`serialization.hpp`), which is a *decode* registry.
/// The type registry never decodes wire bytes; it only answers "what event
/// types exist and what do they look like?". It is fully decoupled from `Bus`:
/// it is populated solely by `CONDUIT_REGISTER_EVENT(T)` and explicit `add<T>()`,
/// never by bus `register_event`/`listen`/`publish`.
///
/// Mirrors the flag machinery (`comms::GlobalFlagRegistry` / `FlagRegistrar` /
/// `COMMONS_REGISTER_FLAG`): a standalone-usable `EventTypeRegistry` plus a
/// program-wide singleton reachable via `global_event_types()`.

#include <conduit/event.hpp>

#include <concepts>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <parcel/parcel.h>

namespace conduit {

/// Identity + shape for one registered event type. The string views and the
/// descriptor handle all have program lifetime (the descriptor is cached in a
/// function-local `static`; the views point into `T::kind_id` storage), so an
/// `EventTypeInfo` is safe to copy and outlive the call that produced it.
struct EventTypeInfo {
    std::string_view name;     ///< Bare event name, e.g. `"order.created"`.
    std::string_view kind_id;  ///< Full wire kind, e.g. `"conduit:event:order.created"`.
    parcel::cell_type_descriptor_t descriptor;

    /// Cell-level display info, lifted straight from the descriptor.
    [[nodiscard]] parcel::DisplayInfo display_info() const {
        return descriptor->display_info();
    }

    /// Per-type JSON schema: `{kind, display_info, category, fields:[…]}`.
    [[nodiscard]] parcel::json_t schema() const {
        return descriptor->to_json();
    }
};

/// Catalog of conduit event types. Wraps a `parcel::ParcelRegistry` (default
/// constructed, so the builtin field/primitive kinds are present and schema
/// work resolves) behind a `std::shared_mutex` — shared lock for reads, unique
/// lock for `add`. Standalone-usable; the process-wide instance is
/// `global_event_types()`.
class EventTypeRegistry {
public:
    EventTypeRegistry() = default;

    /// Register the descriptor for event type `T` (derived from
    /// `conduit::Event<T, Name>`, hence an `ICell`-derived cell).
    template <typename T>
    EventTypeRegistry& add() {
        static_assert(std::derived_from<T, parcel::ICell>,
                      "EventTypeRegistry::add<T>: T must be a parcel ICell-derived event type");
        std::unique_lock lock(mu_);
        registry_.register_kind(T::descriptor());
        return *this;
    }

    /// All registered event types (descriptors whose kind starts with
    /// `event_kind_prefix`; parcel builtins are filtered out).
    [[nodiscard]] std::vector<EventTypeInfo> types() const {
        std::shared_lock lock(mu_);
        std::vector<EventTypeInfo> out;
        for (const auto& desc : registry_.all()) {
            const std::string_view kind = desc->kind();
            if (!kind.starts_with(event_kind_prefix)) {
                continue;
            }
            out.push_back(EventTypeInfo{
                .name = kind.substr(event_kind_prefix.size()),
                .kind_id = kind,
                .descriptor = desc,
            });
        }
        return out;
    }

    /// Look up a type by bare name (`"order.created"`) or full kind
    /// (`"conduit:event:order.created"`). Returns `std::nullopt` if unknown.
    [[nodiscard]] std::optional<EventTypeInfo> find(const std::string_view name_or_kind) const {
        std::shared_lock lock(mu_);
        const auto desc = resolve(name_or_kind);
        if (!desc) {
            return std::nullopt;
        }
        const std::string_view kind = desc->kind();
        return EventTypeInfo{
            .name = kind.substr(event_kind_prefix.size()),
            .kind_id = kind,
            .descriptor = desc,
        };
    }

    /// Whether a type with the given name or kind is registered.
    [[nodiscard]] bool contains(const std::string_view name_or_kind) const {
        std::shared_lock lock(mu_);
        return resolve(name_or_kind) != nullptr;
    }

    /// Per-type JSON schema for the given name or kind.
    /// @throws std::out_of_range if the type is not registered.
    [[nodiscard]] parcel::json_t schema(const std::string_view name_or_kind) const {
        std::shared_lock lock(mu_);
        const auto desc = resolve(name_or_kind);
        if (!desc) {
            throw std::out_of_range{"EventTypeRegistry::schema: unknown event type '" +
                                    std::string{name_or_kind} + "'"};
        }
        return desc->to_json();
    }

private:
    /// Resolve a name-or-kind to a descriptor. Must be called under `mu_`.
    /// Reuses `event_kind_prefix`: a leading-prefix arg is treated as a full
    /// kind, otherwise the prefix is prepended.
    [[nodiscard]] parcel::cell_type_descriptor_t
    resolve(const std::string_view name_or_kind) const {
        if (name_or_kind.starts_with(event_kind_prefix)) {
            return registry_.find(name_or_kind);
        }
        std::string kind;
        kind.reserve(event_kind_prefix.size() + name_or_kind.size());
        kind.append(event_kind_prefix).append(name_or_kind);
        return registry_.find(kind);
    }

    mutable std::shared_mutex mu_;
    parcel::ParcelRegistry registry_;
};

/// Program-wide event type catalog. A Meyers singleton, so it is constructed
/// before any `inline` registrar runs at static-init time.
[[nodiscard]] inline EventTypeRegistry& global_event_types() {
    static EventTypeRegistry r;
    return r;
}

/// Self-registering object: constructing one registers `T` into
/// `global_event_types()`. `CONDUIT_REGISTER_EVENT` emits one as an `inline`
/// object so registration happens at static init.
template <typename T>
struct EventTypeRegistrar {
    EventTypeRegistrar() noexcept {
        global_event_types().add<T>();
    }
};

/// Snapshot of every type registered in the program-wide catalog.
[[nodiscard]] inline std::vector<EventTypeInfo> registered_event_types() {
    return global_event_types().types();
}

}  // namespace conduit

/// Register event type `Ident` into the program-wide `global_event_types()`.
/// Use at namespace scope after the event type is fully defined. Static-init
/// safe (the catalog is a Meyers singleton, built on first use). As with the
/// flag macro, a header-only registrar may be stripped if the type is only ever
/// referenced from headers — pair the macro with a translation unit that uses
/// the type to guarantee the registrar survives.
#define CONDUIT_REGISTER_EVENT(Ident)                                                              \
    inline const ::conduit::EventTypeRegistrar<Ident> conduit_event_type_registrar_##Ident {}
