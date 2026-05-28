#pragma once

/// @file
/// @brief Root exception hierarchy for the conduit library. Every exception
///        intentionally thrown by conduit derives from `conduit::Exception`,
///        so consumers can catch a single library-specific category instead
///        of guessing which `std::*` subtype each layer threw.
///
/// Layout:
///   std::runtime_error
///     conduit::Exception
///       conduit::ConfigError           — transport Config validation failures
///         conduit::TlsNotSupportedError — TLS requested but feature flag off
///       conduit::TransportError        — operational/runtime transport failures
///         (per-transport subclasses live in each transport's public header)
///       conduit::SerializationError    — wire encode/decode failures
///       conduit::UnknownEventTypeError — EventTypeRegistry lookup miss

#include <stdexcept>

namespace conduit {

/// Root of every exception type thrown by conduit. Inherits from
/// `std::runtime_error` so the message-only constructors compose naturally;
/// no extra state or virtual overrides.
class Exception : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Construction-time configuration validation error: a `Config` field was
/// invalid (empty required field, bad enum, etc.) before any IO happened.
class ConfigError : public Exception {
public:
    using Exception::Exception;
};

/// TLS-enabled config was supplied but the relevant transport feature flag was
/// off at build time.
class TlsNotSupportedError : public ConfigError {
public:
    using ConfigError::ConfigError;
};

/// Operational/runtime failure inside a transport adapter (connect, subscribe,
/// publish, etc.). Per-transport subclasses live in each transport's public
/// header (e.g. `conduit::amqp::AmqpError`).
class TransportError : public Exception {
public:
    using Exception::Exception;
};

/// Raised by envelope/cell deserialization when the wire data is malformed.
/// `parcel::ParcelException`-derived errors are mapped here at the registry
/// boundary so callers can catch a single type.
class SerializationError : public Exception {
public:
    using Exception::Exception;
};

/// Raised by `EventTypeRegistry::schema` when the requested name or kind is
/// not registered.
class UnknownEventTypeError : public Exception {
public:
    using Exception::Exception;
};

}  // namespace conduit
