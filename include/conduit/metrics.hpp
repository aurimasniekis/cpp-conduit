#pragma once

/// @file
/// @brief Always-on, backend-agnostic prom metrics for the conduit event system.
///
/// Every accessor returns a long-lived metric handle created on first call via a
/// shared `prom::Scope` named `"conduit"` (metric prefix `"conduit_"`), cached in
/// a static-local. Metrics are **always-on** and safe by default: until a host
/// application installs a backend via
/// `prom::Registry::global()->set_adapter(...)`, every metric op is a noexcept
/// no-op through prom's `NullAdapter`. This header never installs an adapter.
///
/// The recording sites live in `bus.hpp`: the `Bus` instruments its own publish /
/// fan-out / error / subscription paths directly rather than through a metrics
/// middleware. That keeps event-level telemetry on by default with no consumer
/// API call, and — unlike a middleware — still counts envelopes published with
/// `flags::NoMiddleware`, which bypass the middleware pipeline entirely. Pool /
/// worker / thread telemetry comes for free from threadman's own prom
/// instrumentation. Histograms observe **seconds**.
///
/// **Labels.** The bus attaches the most specific label it can derive at each
/// site: event-level series carry `{event=<name>}` (the concrete event name, or
/// a listener's name/pattern for `conduit_listeners`), and
/// `conduit_transport_errors_total` carries `{transport=<adapter>}`. conduit
/// emits whatever is derivable and leaves cardinality control to the consumer —
/// a deployment that doesn't want per-event series drops the label at
/// scrape/relabel time.

#include <prom/prom.hpp>

#include <memory>

namespace conduit::metrics {

/// The shared `conduit` scope; applies the `conduit_` prefix to every metric.
inline std::shared_ptr<prom::Scope>& scope() {
    static std::shared_ptr<prom::Scope> sc =
        prom::scope("conduit",
                    prom::ScopeConfig{.prefix = "conduit_",
                                      .display = comms::DisplayInfo{
                                          .name = "conduit",
                                          .description = "conduit event-bus metrics.",
                                      }});
    return sc;
}

/// Envelopes that entered the dispatch pipeline (one per `Bus::publish`).
inline prom::Counter& events_published() {
    static prom::Counter m =
        scope()->counter({.name = "events_published_total",
                          .help = "Envelopes that entered the dispatch pipeline."});
    return m;
}

/// Envelopes dropped before listener fan-out (a middleware `before_dispatch`
/// returned false, or a routing conflict).
inline prom::Counter& events_dropped() {
    static prom::Counter m = scope()->counter(
        {.name = "events_dropped_total",
         .help = "Envelopes dropped before dispatch (middleware veto or routing conflict)."});
    return m;
}

/// Listener handler calls during fan-out (counts every matched listener, summed
/// over all delivered envelopes).
inline prom::Counter& listener_invocations() {
    static prom::Counter m =
        scope()->counter({.name = "listener_invocations_total",
                          .help = "Listener handler calls during fan-out (per matched listener)."});
    return m;
}

/// Listener handlers that threw during fan-out.
inline prom::Counter& listener_errors() {
    static prom::Counter m = scope()->counter(
        {.name = "listener_errors_total", .help = "Listener handlers that threw during fan-out."});
    return m;
}

/// Transport-level inbound failures, selected by the `transport` label
/// (`"redis"`, `"amqp"`, `"zmq"`, `"mqtt"`, `"nats"`, …).
inline prom::Counter& transport_errors() {
    static prom::Counter m = scope()->counter(
        {.name = "transport_errors_total",
         .help = "Transport-level inbound failures (e.g. an undecodable message)."});
    return m;
}

/// Live listener subscriptions across all buses in the process.
inline prom::Gauge& listeners() {
    static prom::Gauge m = scope()->gauge(
        {.name = "listeners", .help = "Live listener subscriptions registered on a bus."});
    return m;
}

/// Wall-clock seconds from publish to the end of the synchronous dispatch
/// pipeline. For asynchronous transports (Queue / ThreadPool) this measures the
/// enqueue + routing portion, not the listener execution that runs off-thread.
inline prom::Histogram& dispatch_seconds() {
    static prom::Histogram m = scope()->histogram(
        {.name = "dispatch_seconds",
         .help =
             "Wall-clock seconds from publish to the end of the synchronous dispatch pipeline."});
    return m;
}

}  // namespace conduit::metrics
