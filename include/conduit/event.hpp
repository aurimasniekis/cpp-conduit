#pragma once

/// @file
/// @brief `Event<Self, Name>` library base built on `parcel::SelfStructCell`.
///
/// User events derive from `conduit::Event<Self, "name">` and declare their
/// fields via a static `event_field_descriptors(parcel::FieldsBuilder<Self>&)`
/// hook. Serialization and deserialization are auto-derived by cpp-parcel.

#include <conduit/flags.hpp>

#include <string_view>

#include <parcel/parcel.h>

namespace conduit {

/// CRTP library base for conduit events.
///
/// `Self` is the deriving event type (CRTP self); `Name` is the bare event
/// name (no prefix). The wire `kind_id` is synthesized as
/// `"conduit:event:<Name>"`; the bare event name is exposed via
/// `event_name_v` for bus dispatch.
///
/// Each user event must provide:
///   - `static auto& event_field_descriptors(parcel::FieldsBuilder<Self>&)` —
///     declares the event's fields.
///   - a public default constructor (parcel's `from_json` builds via
///     `std::make_shared<Self>()`).
template <typename Self, parcel::FixedString Name>
class Event : public parcel::SelfStructCell<Self> {
public:
    /// Wire-stable kind id (`"conduit:event:" + Name`).
    static constexpr std::string_view kind_id = parcel::id_join_lit_v<"conduit:event:", Name>;

    /// Bare event name (`Name`) — used by the bus to key listeners.
    static constexpr std::string_view event_name_v = Name.view();

    static auto field_descriptors() {
        parcel::FieldsBuilder<Self> b;
        return Self::event_field_descriptors(b).build();
    }

    /// Cell-level descriptive display info. Defaults to empty; user events may
    /// shadow this static to attach `name`/`description` for tooling.
    static parcel::DisplayInfo display_info() {
        return {};
    }
};

// ---------------------------------------------------------------------------
// DefaultFlags<...> mixin + event_traits<T> trait for declaring default flags
// applied automatically by the builder.
// ---------------------------------------------------------------------------

/// Mixin marker. Inheriting from `DefaultFlags<Fs...>` on an event class causes
/// the EventBuilder to OR these flags into the envelope at build time.
template <typename... Fs>
struct DefaultFlags {
    using conduit_default_flags_tag = void;
    static flags::FlagSet default_flags_value() {
        return flags::FlagSet::of<Fs...>();
    }
};

/// Non-intrusive trait. Specialize to attach default flags to a foreign event
/// type that cannot inherit from `DefaultFlags<...>`.
template <typename T>
struct event_traits {  // NOLINT(readability-identifier-naming)
    static flags::FlagSet default_flags() {
        return flags::FlagSet{};
    }
};

namespace detail {

template <typename T>
concept HasDefaultFlagsMixin = requires { typename T::conduit_default_flags_tag; };

template <typename T>
[[nodiscard]] inline flags::FlagSet collect_default_flags() {
    flags::FlagSet s = event_traits<T>::default_flags();
    if constexpr (HasDefaultFlagsMixin<T>) {
        for (const auto& f : T::default_flags_value()) {
            s.insert(f);
        }
    }
    return s;
}

}  // namespace detail

}  // namespace conduit
