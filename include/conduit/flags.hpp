#pragma once

/// @file
/// @brief Conduit flag tags, built atop `comms::Flag` / `comms::FlagSet`.
///
/// Each conduit flag is a distinct type deriving from `comms::Flag<Name,
/// ConduitFlagCategory>`. The fixed-string name is the flag's stable identity —
/// across processes, across translation units, and on the wire — so
/// serialization just lists the names directly. Every built-in flag also
/// carries a `comms::DisplayInfo` (name, description, `mdi` icon, MUI palette
/// color) so UI tooling that consumes `comms::DisplayInfo` can render conduit
/// flags out of the box.

#include <commons/color.hpp>
#include <commons/display_info.hpp>
#include <commons/flag.hpp>
#include <commons/icon.hpp>
#include <commons/icons.hpp>

namespace conduit::flags {

/// Single category for all conduit-defined flags. Downstream code defining its
/// own flag types via `conduit::flags::Flag<"my.flag">` lands here too unless
/// it picks a different category explicitly.
struct ConduitFlagCategory : comms::FlagCategory<"conduit"> {};

/// Template alias mirroring `comms::Flag` with `ConduitFlagCategory` as the
/// default — preserves the old `Flag<"name">` syntax for downstream code.
template <comms::FixedString Name, typename Category = ConduitFlagCategory>
using Flag = comms::Flag<Name, Category>;

using FlagSet = comms::FlagSet;

// ---------------------------------------------------------------------------
// Built-in flag tag types. Each carries a `DisplayInfo` (mdi icon + Colors::mui
// palette color) and is registered with the `comms::GlobalFlagRegistry` so its
// wire-name round-trips through `FlagSetCell`.
// ---------------------------------------------------------------------------

/// Force same-thread dispatch even in Queue / ThreadPool execution modes.
struct Direct : Flag<"direct"> {
    static const comms::DisplayInfo& display_info() {
        static const comms::DisplayInfo info{
            .name = "Direct",
            .description = "Force same-thread dispatch even in Queue / ThreadPool execution modes.",
            .icon = comms::Icons::mdi::flash,
            .color = comms::Colors::mui::yellow_700,
        };
        return info;
    }
};

/// Hint to durable transports that they should persist the envelope.
struct Durable : Flag<"durable"> {
    static const comms::DisplayInfo& display_info() {
        static const comms::DisplayInfo info{
            .name = "Durable",
            .description = "Hint to durable transports that they should persist the envelope.",
            .icon = comms::Icons::mdi::database,
            .color = comms::Colors::mui::blue_700,
        };
        return info;
    }
};

/// Hint to transports to use a persistent (human-readable) wire format.
struct Persistent : Flag<"persistent"> {
    static const comms::DisplayInfo& display_info() {
        static const comms::DisplayInfo info{
            .name = "Persistent",
            .description = "Hint to transports to use a persistent (human-readable) wire format.",
            .icon = comms::Icons::mdi::file_document_outline,
            .color = comms::Colors::mui::teal_500,
        };
        return info;
    }
};

/// Skip the middleware pipeline for this envelope.
struct NoMiddleware : Flag<"no_middleware"> {
    static const comms::DisplayInfo& display_info() {
        static const comms::DisplayInfo info{
            .name = "NoMiddleware",
            .description = "Skip the middleware pipeline for this envelope.",
            .icon = comms::Icons::mdi::pipe_disconnected,
            .color = comms::Colors::mui::grey_600,
        };
        return info;
    }
};

/// Request the broker / transport acknowledge the publish before returning.
struct RequireAck : Flag<"require_ack"> {
    static const comms::DisplayInfo& display_info() {
        static const comms::DisplayInfo info{
            .name = "RequireAck",
            .description =
                "Request the broker / transport acknowledge the publish before returning.",
            .icon = comms::Icons::mdi::check_decagram,
            .color = comms::Colors::mui::green_600,
        };
        return info;
    }
};

/// Hint that this envelope is intended for fan-out broadcast.
struct Broadcast : Flag<"broadcast"> {
    static const comms::DisplayInfo& display_info() {
        static const comms::DisplayInfo info{
            .name = "Broadcast",
            .description = "Hint that this envelope is intended for fan-out broadcast.",
            .icon = comms::Icons::mdi::broadcast,
            .color = comms::Colors::mui::deep_orange_500,
        };
        return info;
    }
};

/// Restrict dispatch to transports with `TransportScope::Local`.
struct LocalOnly : Flag<"local_only"> {
    static const comms::DisplayInfo& display_info() {
        static const comms::DisplayInfo info{
            .name = "LocalOnly",
            .description = "Restrict dispatch to transports with TransportScope::Local.",
            .icon = comms::Icons::mdi::home_circle,
            .color = comms::Colors::mui::light_blue_500,
        };
        return info;
    }
};

/// Restrict dispatch to transports with `TransportScope::Remote`.
struct RemoteOnly : Flag<"remote_only"> {
    static const comms::DisplayInfo& display_info() {
        static const comms::DisplayInfo info{
            .name = "RemoteOnly",
            .description = "Restrict dispatch to transports with TransportScope::Remote.",
            .icon = comms::Icons::mdi::earth,
            .color = comms::Colors::mui::indigo_500,
        };
        return info;
    }
};

// Self-register the built-ins so `FlagSetCell` can resolve them by name on
// decode and so `GlobalFlagRegistry::find("direct")` etc. just work.
COMMONS_REGISTER_FLAG(Direct);
COMMONS_REGISTER_FLAG(Durable);
COMMONS_REGISTER_FLAG(Persistent);
COMMONS_REGISTER_FLAG(NoMiddleware);
COMMONS_REGISTER_FLAG(RequireAck);
COMMONS_REGISTER_FLAG(Broadcast);
COMMONS_REGISTER_FLAG(LocalOnly);
COMMONS_REGISTER_FLAG(RemoteOnly);

}  // namespace conduit::flags
