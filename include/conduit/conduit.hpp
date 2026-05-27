#pragma once

/// @file
/// @brief Umbrella header — includes the entire conduit core API.
///
/// Transport adapters that ship as separate CMake subprojects (e.g. the MQTT
/// adapter) provide their own headers under `<conduit/<name>/transport.hpp>`
/// and are NOT pulled in by this umbrella.

#include <conduit/builder.hpp>
#include <conduit/bus.hpp>
#include <conduit/envelope.hpp>
#include <conduit/event.hpp>
#include <conduit/event_type_registry.hpp>
#include <conduit/filtered_transport.hpp>
#include <conduit/flags.hpp>
#include <conduit/glob.hpp>
#include <conduit/listener.hpp>
#include <conduit/local/executor.hpp>
#include <conduit/local/transport.hpp>
#include <conduit/metadata.hpp>
#include <conduit/middleware.hpp>
#include <conduit/parcel_cells.hpp>
#include <conduit/relay/transport.hpp>
#include <conduit/serialization.hpp>
#include <conduit/transport.hpp>
#include <conduit/version.hpp>
