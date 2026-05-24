#pragma once

/// @file
/// @brief Envelope metadata container and lifecycle timestamps.

#include <md/metadata.hpp>

#include <chrono>
#include <optional>

namespace conduit {

/// Envelope metadata: a typed JSON-shaped key/value tree.
///
/// Backed by `md::Metadata` (= `md::Object`), so values may be any of
/// `null`, `bool`, `int64`, `uint64`, `float`, `double`, `string`,
/// `Array`, or nested `Object`. See the `<md/metadata.hpp>` documentation
/// for the full API (`require_string`, `get_string_if`, `find_path`,
/// `merge`, …).
using Metadata = md::Metadata;

/// Lifecycle timestamps tracked by the bus / middleware / transports.
struct Timestamps {
    std::chrono::system_clock::time_point created_at;
    std::optional<std::chrono::system_clock::time_point> published_at;
    std::optional<std::chrono::system_clock::time_point> received_at;
    std::optional<std::chrono::system_clock::time_point> delivered_at;
    std::optional<std::chrono::system_clock::time_point> failed_at;
};

}  // namespace conduit
