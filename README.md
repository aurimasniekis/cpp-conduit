# Conduit

[![CI](https://github.com/aurimasniekis/cpp-conduit/actions/workflows/ci.yml/badge.svg)](https://github.com/aurimasniekis/cpp-conduit/actions/workflows/ci.yml)
[![Docs](https://github.com/aurimasniekis/cpp-conduit/actions/workflows/docs.yml/badge.svg)](https://aurimasniekis.github.io/cpp-conduit/)

`conduit` is a C++23 event bus and event-transport library. You declare events as C++ types with compile-time names, hand them to a `Bus`, and the bus delivers them — synchronously, on a thread pool, or out over MQTT / AMQP / NATS / Redis / ZeroMQ. The same event object also serializes itself to JSON or CBOR so it can leave the process and come back.

## Why use this library?

- Good for **decoupling producers from consumers** inside one process — and then later extending the same code to publish off-machine without rewriting the producer site.
- Good for **typed in-process pub/sub** where you want compile-time event names and listener handlers that take the payload type directly (`[](const OrderCreated& o){...}`).
- Good for **bridging a bus to an external system** — the `relay` transport hands matching envelopes to a user callback; point it at your websocket / HTTP webhook / log sink.
- Useful when you want **glob-pattern listeners** (`bus.listen("order.**", ...)`) without rolling your own matcher.
- Useful when you want **middleware around every dispatch** — tracing, metrics, deny-listing, structured logging.
- **Not ideal for** hard-real-time work: dispatch goes through `std::function`, `std::mutex`, and (for broker transports) heap-allocated wire buffers. It is fire-and-forget by default — failures are reported through middleware, not thrown at the publisher.
- **Not ideal for** guaranteed delivery on its own. The core ferries `Durable` / `Persistent` / `RequireAck` flags through, but actual durability is the broker adapter's job (MQTT QoS, AMQP `confirm.select`, etc.).

## Quick example

```cpp
#include <conduit/conduit.hpp>

#include <iostream>
#include <string>

#include <parcel/parcel.h>

// An event type. The fixed-string `"greeted"` is the wire name used by
// listeners and any remote transport. The static `event_field_descriptors`
// hook declares the fields that get serialized.
struct Greeted : conduit::Event<Greeted, "greeted"> {
    std::string who;

    Greeted() = default;
    explicit Greeted(std::string s) : who(std::move(s)) {}

    static auto& event_field_descriptors(parcel::FieldsBuilder<Greeted>& b) {
        return b.field<&Greeted::who>("who");
    }
};

int main() {
    conduit::Bus bus;
    bus.use_transport<conduit::local::Transport>();   // in-process delivery

    // listen<T>(handler) — handler may take `const T&` or `const EventEnvelope&`.
    // `sub` keeps the subscription alive; destroy it to unsubscribe.
    auto sub = bus.listen<Greeted>([](const Greeted& g) {
        std::cout << "hello, " << g.who << '\n';
    });

    bus.publish(conduit::event(Greeted{"world"}).build());
}
```

A few things to notice in this example:

- `Greeted` derives from `conduit::Event<Greeted, "greeted">`. The CRTP parameter is the event class itself; the fixed-string is the event's wire-stable name.
- `event_field_descriptors` is required (even for empty events — return `b;`). It is how `parcel` learns to encode the event for transport.
- `bus.publish(...)` is `void` and does not throw on listener errors; exceptions are routed to middleware (see *Error handling*).
- The `sub` handle is RAII: when it is destroyed the listener is unregistered.

## Installation

`conduit` is a CMake project. The core library is header-only; each broker adapter is an opt-in static library gated by `CONDUIT_TRANSPORT_<NAME>=ON`.

### CMake FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    conduit
    URL      https://github.com/aurimasniekis/cpp-conduit/archive/refs/tags/v0.2.0.tar.gz
    URL_HASH SHA256=0000000000000000000000000000000000000000000000000000000000000000  # TODO: fill in after tagging v0.2.0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(conduit)

target_link_libraries(my_app PRIVATE conduit::conduit)
```

To opt into a transport adapter — for example MQTT — set the option *before* fetching:

```cmake
set(CONDUIT_TRANSPORT_MQTT ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(conduit)

target_link_libraries(my_app PRIVATE
    conduit::conduit
    conduit::transport_mqtt)
```

### `find_package` after install

`conduit` generates an install rule when it is built top-level and none of its dependencies were pulled via `FetchContent`. After a regular `cmake --install`, downstream projects can:

```cmake
find_package(conduit REQUIRED)
target_link_libraries(my_app PRIVATE conduit::conduit)
```

### `add_subdirectory`

Drop the repo into a `third_party/` folder and `add_subdirectory(third_party/conduit)`. The library exports `conduit::conduit` and one `conduit::transport_<name>` target per enabled adapter.

### Minimal consumer `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.25)
project(my_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    conduit
    URL      https://github.com/aurimasniekis/cpp-conduit/archive/refs/tags/v0.2.0.tar.gz
    URL_HASH SHA256=0000000000000000000000000000000000000000000000000000000000000000  # TODO: fill in after tagging v0.2.0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(conduit)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE conduit::conduit)
```

## Requirements

- C++23 compiler.
- CMake ≥ 3.25.
- Always-fetched dependencies:
  - `nlohmann/json` 3.12.0
  - `cpp-ulid` 1.0.0
  - `cpp-parcel` 0.2.0
  - `cpp-metadata` 0.2.0
  - `cpp-commons` 0.1.3
- Tests use GoogleTest 1.15.2 (only when `CONDUIT_BUILD_TESTS=ON`, default on top-level builds).
- Each transport adapter brings its own dependencies, only when its option is enabled:

  | Option                    | Pulls in                                                                |
  |---------------------------|-------------------------------------------------------------------------|
  | `CONDUIT_TRANSPORT_MQTT`  | Paho MQTT C++ + Paho MQTT C                                             |
  | `CONDUIT_TRANSPORT_AMQP`  | AMQP-CPP (optional OpenSSL with `CONDUIT_TRANSPORT_AMQP_TLS`)           |
  | `CONDUIT_TRANSPORT_NATS`  | nats.c (optional OpenSSL with `CONDUIT_TRANSPORT_NATS_TLS`)             |
  | `CONDUIT_TRANSPORT_REDIS` | redis-plus-plus + hiredis (optional TLS)                                |
  | `CONDUIT_TRANSPORT_ZMQ`   | libzmq + cppzmq (optional libsodium with `CONDUIT_TRANSPORT_ZMQ_CURVE`) |

- Threads — `find_package(Threads REQUIRED)` is unconditional.

## Core concepts

### `conduit::Event<Self, Name>`

User events derive from this CRTP base. `Self` is the event class itself; `Name` is the wire-stable name as a `parcel::fixed_string`. Each event must provide a static `event_field_descriptors` hook that declares its fields — `parcel` uses it for JSON/CBOR encode and decode.

```cpp
struct OrderCreated : conduit::Event<OrderCreated, "order.created"> {
    std::string order_id;
    double total = 0.0;

    OrderCreated() = default;
    OrderCreated(std::string id, double t)
        : order_id(std::move(id)), total(t) {}

    static auto& event_field_descriptors(parcel::FieldsBuilder<OrderCreated>& b) {
        return b.field<&OrderCreated::order_id>("order_id")
                .field<&OrderCreated::total>("total");
    }
};
```

An event with no fields still needs the hook — return the builder unchanged:

```cpp
struct Tick : conduit::Event<Tick, "tick"> {
    static auto& event_field_descriptors(parcel::FieldsBuilder<Tick>& b) {
        return b;
    }
};
```

Events must be default-constructible: the registry decodes by calling `std::make_shared<T>()` and then populating fields.

### `conduit::EventEnvelope`

The polymorphic wrapper that flows through the bus. It carries:

- A ULID `id()`.
- `flags()` — see *Flags*.
- `metadata()` — a JSON-shaped key/value tree backed by [`md::Metadata`](https://github.com/aurimasniekis/cpp-metadata). Values may be strings, booleans, integers, floats, arrays, or nested objects; access via `require_string("k")` / `get_string_if("k")` / `at("k").as_int()` etc., or path-style with `require_path("device.firmware.major")`.
- `timestamps()` — `created_at`, `published_at`, `received_at`, `delivered_at`, `failed_at`.
- Optional `correlation_id()` and `causation_id()` (ULIDs).
- The typed payload, accessed with `payload_as<T>()` returning `std::shared_ptr<const T>` (or `nullptr` if the payload is not a `T`).

`EventEnvelopeView` is a source-compatibility alias for `EventEnvelope`. The bus stores envelopes by `shared_ptr` internally, so copying an envelope is cheap and the core is shared — mutations through a non-const accessor on a copy show up on every other copy.

### `EventBuilder<T>` and `conduit::event(T)`

A fluent builder. Returned by `conduit::event(payload)`; finalized with `.build()` or by implicit conversion to `EventEnvelope` (used by `bus.publish(builder)`):

```cpp
auto env = conduit::event(OrderCreated{"O-9", 49.99})
    .metadata("tenant", "acme")
    .correlation_id(parent_id)
    .flag<conduit::flags::Direct>()
    .build();
bus.publish(env);

// Or pass the payload straight to publish() — the bus wraps it with defaults.
bus.publish(OrderCreated{"O-9", 49.99});
```

### `conduit::Bus`

Owns transports, middleware, and listeners. Constructible on the stack or via `shared_ptr` — the bus tracks subscriptions through a self-aliasing pointer so both work. `Bus` is non-copyable and non-movable; pass it by reference.

The bus is destroyed last: its destructor calls `shutdown()`, which flushes and detaches every transport. `shutdown()` is idempotent and noexcept.

### `Transport`

An abstract base for anything that carries envelopes. The library ships with:

- `conduit::local::Transport` — in-process. Three modes: `Direct` (same thread), `Queue` (single worker), `ThreadPool` (N workers).
- `conduit::relay::Transport` — hands envelopes whose name matches a glob to a user callback.
- `conduit::FilteredTransport` — wraps another transport with outbound/inbound predicates.
- `conduit::mqtt::Transport`, `amqp::Transport`, `nats::Transport`, `redis::Transport`, `zmq::Transport` — opt-in broker adapters.

A transport returns `TransportScope::Local` or `TransportScope::Remote` from `scope()`. The bus uses that together with the `LocalOnly` / `RemoteOnly` flags on an envelope to decide where it goes.

### `Middleware`

Hooks invoked around every publish:

```cpp
class Middleware {
public:
    virtual bool before_dispatch(EventEnvelopeView& v);
    virtual void after_dispatch(EventEnvelopeView& v);
    virtual void on_error(EventEnvelopeView& v, const std::exception_ptr& ep);
    virtual void on_transport_error(std::string_view transport,
                                    const std::exception_ptr& ep);
};
```

- `before_dispatch` can return `false` to drop the envelope.
- `on_error` fires when a listener throws (or when an invariant like `LocalOnly + RemoteOnly` is violated).
- `on_transport_error` fires when a transport adapter fails to decode an inbound message — there is no envelope at that point, so the hook receives the transport's short name and the exception.

### Subscriptions

`Bus::listen(...)` returns a `Subscription` (a move-only RAII handle). Drop it to unregister. `Subscription::detach()` and `Subscription::release()` exist; prefer letting the handle's destructor do the work.

```cpp
{
    auto sub = bus.listen<Greeted>([](const Greeted&) {});
    bus.publish(Greeted{"world"});            // delivered
} // sub goes out of scope here
bus.publish(Greeted{"world"});                // not delivered
```

`EventSubscriber` is a base class that owns several `Subscription`s in one place — convenient for projection / aggregator objects that listen to many events.

## Common usage patterns

### Listener styles

```cpp
// Take the payload directly.
auto a = bus.listen<OrderCreated>([](const OrderCreated& o) { /* ... */ });

// Take the envelope — gives you id, flags, metadata, timestamps.
auto b = bus.listen<OrderCreated>([](const conduit::EventEnvelope& env) {
    auto p = env.payload_as<OrderCreated>();
    std::cout << env.id().string() << ' ' << p->order_id << '\n';
});

// Exact name string (no wildcards).
auto c = bus.listen("order.created", [](const conduit::EventEnvelopeView&) {});

// Glob pattern (`*` = within segment, `**` = across segments).
auto d = bus.listen("order.**", [](const conduit::EventEnvelopeView& v) {
    std::cout << v.name() << '\n';
});

// Class-based listener.
class MyListener : public conduit::EventListener<OrderCreated> {
public:
    void on_event(const conduit::EventEnvelope<OrderCreated>&) override {}
};
auto e = bus.listen<OrderCreated>(std::make_shared<MyListener>());
```

`bus.listen<T>(handler)` registers the event type with the bus's registry automatically, so a remote transport on the same bus can decode incoming `T` envelopes too.

### Wiring up several listeners with `EventSubscriber`

```cpp
class OrderProjection : public conduit::EventSubscriber {
public:
    void register_to(conduit::Bus& bus) override {
        on<OrderCreated>(bus, [](const OrderCreated& o) { /* ... */ });
        on<OrderShipped>(bus, [](const OrderShipped& o) { /* ... */ });
        on(bus, "audit.*", [](const conduit::EventEnvelopeView&) {});
    }
};

OrderProjection projection;
bus.register_subscriber(projection);
```

When `projection` is destroyed, every subscription it owns is cleaned up.

### Picking a local execution mode

```cpp
// Direct (default): each publish() delivers in the caller's thread.
bus.use_transport<conduit::local::Transport>();

// Queue: one worker thread drains in FIFO order. publish() returns immediately.
bus.use_transport<conduit::local::Transport>(conduit::local::Execution::Queue);

// ThreadPool: N workers. Use ThreadPoolConfig::queue_capacity for backpressure.
bus.use_transport<conduit::local::Transport>(
    conduit::local::Execution::ThreadPool,
    conduit::local::ThreadPoolConfig{.threads = 4, .queue_capacity = 256});
```

`queue_capacity = 0` is unbounded — convenient for tests, hazardous in production if a consumer falls behind. Set a positive cap so `submit()` blocks the producer when the queue is full.

`flags::Direct` on an envelope overrides the mode and forces inline delivery even when the local transport is queued or pooled — useful for "this must happen synchronously" events like config reload:

```cpp
bus.publish(conduit::event(Beep{}).flag<conduit::flags::Direct>().build());
```

### Relaying selected events outside the bus

```cpp
bus.use_transport<conduit::relay::Transport>("order.*",
    [](const conduit::EventEnvelopeView& v) {
        const auto j = conduit::serialization::encode_json(v);
        std::cout << "relayed: " << j.dump() << '\n';
    });

bus.publish(conduit::event(OrderCreated{"O-9", 49.99}).build()); // relayed
bus.publish(conduit::event(AuditRecorded{}).build());            // not relayed
```

`relay::Transport` accepts a glob; you can `add_route` / `remove_route` at runtime. Callbacks run on whatever thread `dispatch()` happens to be called from — usually the publisher's, unless a `local::Transport` in Queue/ThreadPool mode is in front of you. Exceptions thrown from a relay callback are swallowed so the bus's fire-and-forget contract holds.

### Filtering a transport bidirectionally

`FilteredTransport` wraps an inner transport with up to two predicates. Outbound gating decides what `dispatch()` actually sends; inbound gating decides what the bus sees from arrivals.

```cpp
auto inner = std::make_shared<conduit::mqtt::Transport>(cfg);
bus.use_transport<conduit::FilteredTransport>(
    inner,
    /*outbound=*/[](const auto& v){ return conduit::Glob::match("order.*", v.name()); },
    /*inbound=*/ [](const auto& v){ return !v.flags().template has<conduit::flags::LocalOnly>(); });
```

Either predicate may be empty (`{}`) — meaning "pass everything".

### Middleware

```cpp
class TraceMW : public conduit::Middleware {
    bool before_dispatch(conduit::EventEnvelopeView& v) override {
        v.metadata().insert_or_assign("trace_id", make_trace_id());
        return true;
    }
    void on_error(conduit::EventEnvelopeView& v, const std::exception_ptr&) override {
        log_error(v.name());
    }
    void on_transport_error(std::string_view transport,
                            const std::exception_ptr& ep) override {
        try { std::rethrow_exception(ep); }
        catch (const std::exception& e) {
            log_error("transport " + std::string{transport} + " failed: " + e.what());
        }
    }
};

bus.use_middleware<TraceMW>();
```

Skip the whole pipeline for one envelope with `flags::NoMiddleware`:

```cpp
bus.publish(conduit::event(Spammy{}).flag<conduit::flags::NoMiddleware>().build());
```

### Scope-aware dispatch and default flags

```cpp
struct AppConfigReloadEvent
    : conduit::Event<AppConfigReloadEvent, "app.config.reload">,
      conduit::DefaultFlags<conduit::flags::LocalOnly> { /* ... */ };
```

Every published `AppConfigReloadEvent` automatically has `LocalOnly` set; the bus will route it through local listeners but skip any `TransportScope::Remote` transport. If you can't modify the event type, specialize the trait:

```cpp
namespace conduit {
template <>
struct event_traits<ThirdPartyEvent> {
    static flags::FlagSet default_flags() {
        return flags::FlagSet::of<flags::LocalOnly>();
    }
};
}
```

Routing matrix (from `Bus::publish_impl`):

| envelope flags | transport scope | dispatched?                    |
|----------------|-----------------|--------------------------------|
| `LocalOnly`    | `Local`         | yes                            |
| `LocalOnly`    | `Remote`        | no — silently skipped          |
| `RemoteOnly`   | `Local`         | no                             |
| `RemoteOnly`   | `Remote`        | yes                            |
| both set       | any             | dropped + routed to `on_error` |
| neither set    | any             | yes                            |

Custom flags follow the same `Flag<"name">` pattern:

```cpp
struct MyAuditFlag : conduit::flags::Flag<"my.audit"> {};
bus.publish(conduit::event(X{}).flag<MyAuditFlag>());
```

The fixed-string name is the flag's stable identity on the wire — no registration step.

### Serialization

```cpp
auto env = conduit::event(Telemetry{"t-1", 0.5})
               .metadata("source", "sensor-3").build();

auto j     = conduit::encode_json(env);   // nlohmann::json
auto bytes = conduit::encode_cbor(env);   // std::vector<std::uint8_t>

conduit::EventRegistry reg;
reg.add<Telemetry>();

auto env2 = reg.decode_json(j);
auto env3 = reg.decode_cbor(std::span<const std::uint8_t>{bytes});
auto pay  = env2.payload_as<Telemetry>();
```

The wire shape is stable:

```json
{
  "id": "01H...",
  "name": "telemetry",
  "flags": ["direct", "durable"],
  "metadata": { "source": "sensor-3" },
  "timestamps": { "created_at": 1779378065775 },
  "correlation_id": "01H...",
  "causation_id":   "01H...",
  "payload": { "id": "t-1", "value": 0.5 }
}
```

Each flag's wire name is the literal passed to `Flag<"...">`, so flag identity survives across processes and language boundaries without registration. `serialization::encode_json` / `encode_cbor` / `EventRegistry` are also exposed under the `conduit::serialization::` namespace for backwards compatibility.

### Talking to a real broker (MQTT example)

```cpp
#include <conduit/mqtt/transport.hpp>

conduit::mqtt::Config cfg;
cfg.url       = "tcp://localhost:1883";
cfg.client_id = "my-app";
cfg.qos       = 1;
cfg.topic     = "conduit/orders";   // required, non-empty

bus.use_transport<conduit::mqtt::Transport>(cfg);
```

One `mqtt::Transport` instance binds to a single topic and carries traffic in both directions. To route different events onto different topics, attach a second instance with its own `Config::topic`, optionally wrapped in `FilteredTransport`. Other broker adapters (AMQP, NATS, Redis, ZMQ) follow the same shape — one transport instance per logical channel; see the per-adapter examples under `transports/<name>/examples/`.

## Error handling

`conduit` is fire-and-forget. `Bus::publish` returns `void` and does not throw under normal operation. Failures are surfaced through middleware:

| Failure                                                        | Path                                                                                       |
|----------------------------------------------------------------|--------------------------------------------------------------------------------------------|
| Listener throws                                                | `Middleware::on_error(envelope, exception_ptr)`                                            |
| `LocalOnly + RemoteOnly` set on the same envelope              | `on_error` with `std::runtime_error("LocalOnly + RemoteOnly conflict — envelope dropped")` |
| Transport's `dispatch` throws                                  | `on_error` with the offending envelope                                                     |
| Transport fails to decode an inbound message (no envelope yet) | `Middleware::on_transport_error(transport_name, exception_ptr)`                            |
| Relay callback throws                                          | swallowed — the bus contract is fire-and-forget                                            |
| Middleware itself throws                                       | swallowed inside the pipeline                                                              |

Configuration-time failures **do** throw:

- `mqtt::Transport`, `nats::Transport`, `amqp::Transport`, `redis::Transport`, `zmq::Transport` throw `std::invalid_argument` from their constructor if required fields are empty (topic / subject / channel / etc.).
- `attach()` throws `std::runtime_error` if it cannot connect to the broker — caught by `Bus::use_transport` only if the user wraps it; otherwise it propagates out.

A typical pattern:

```cpp
class LogErrors : public conduit::Middleware {
    void on_error(conduit::EventEnvelopeView& v, const std::exception_ptr& ep) override {
        try { std::rethrow_exception(ep); }
        catch (const std::exception& e) {
            std::cerr << "dispatch error on " << v.name() << ": " << e.what() << '\n';
        }
    }
    void on_transport_error(std::string_view t, const std::exception_ptr& ep) override {
        try { std::rethrow_exception(ep); }
        catch (const std::exception& e) {
            std::cerr << "transport " << t << " error: " << e.what() << '\n';
        }
    }
};

bus.use_middleware<LogErrors>();
```

## Edge cases and pitfalls

- **A `Subscription` you forget to keep alive cancels immediately.** `bus.listen(...)` returns a `[[nodiscard]]` handle whose destructor unsubscribes. `bus.listen<T>([]{ ... });` (no assignment) is almost always a bug.
- **`bus.publish(...)` with no transport attached** still delivers to local listeners — there is a fallback that fans out inline. This is convenient for tests; in production, attach at least one transport so the routing matrix runs.
- **`local::Transport` in Queue / ThreadPool mode** runs listeners on a different thread. Capture-by-reference into `bus.listen` requires that the captured object outlive the bus. `bus.drain()` waits for the current backlog; `bus.shutdown()` is implicit in the destructor and is idempotent.
- **`queue_capacity = 0` is unbounded.** A slow consumer + fast producer + unbounded queue is the classic memory-leak shape. Pick a real number.
- **Events must be default-constructible.** The registry deserializes by `std::make_shared<T>()` and then populates fields. A missing default constructor is a compile error inside `parcel`'s machinery — the message is long; the fix is short.
- **`payload_as<T>()` returns `nullptr` when the envelope's payload is some other type.** This happens when a pattern listener (`"order.*"`) fires for an event whose C++ type the listener does not know — always null-check before dereferencing.
- **Inbound decode errors are swallowed by the transport** and reported via `Middleware::on_transport_error`. If you do not install a middleware that handles it, malformed wire data is silently dropped.
- **`LocalOnly` and `RemoteOnly` set on the same envelope** is treated as an invariant violation: the envelope is dropped and `on_error` fires. The builder will happily let you do it, so prefer setting one or the other.
- **`Bus` cannot be moved or copied.** Use a `shared_ptr<Bus>` if you need shared ownership; the bus keeps an internal self-alias so `shared_from_this()` works either way.
- **Mutations on an envelope copy mutate the original.** Envelopes share their core via `shared_ptr`. This is intentional — it is how middleware can stamp `trace_id` into metadata and have it reach the listeners. It also means `local.timestamps().received_at = ...` in a transport is visible everywhere.
- **`flags::NoMiddleware` skips the whole middleware pipeline**, including your audit logging. Use it carefully — it is meant for noisy traffic that is already accounted for, not as a generic opt-out.
- **Relay callbacks share the publisher's thread** when no thread-pool transport is in front of them. If your callback blocks, the publisher blocks.
- **Broker connections happen during `attach`.** `bus.use_transport<conduit::mqtt::Transport>(cfg)` will block until it connects or fails — be prepared for `std::runtime_error` at startup.
- **Pattern listeners use globs, not regex.** `*` matches within one segment (`order.*` matches `order.created` but not `order.line.added`); `**` crosses segments. Anything else matches literally.

## API overview

| API                                                       | Lives in                            | Purpose                                                                   |
|-----------------------------------------------------------|-------------------------------------|---------------------------------------------------------------------------|
| `conduit::Event<Self, "name">`                            | `conduit/event.hpp`                 | CRTP base for user events.                                                |
| `conduit::DefaultFlags<...>` / `event_traits<T>`          | `conduit/event.hpp`                 | Attach default flags to an event type.                                    |
| `conduit::EventEnvelope` (alias `EventEnvelopeView`)      | `conduit/envelope.hpp`              | The envelope passed around the bus.                                       |
| `conduit::EventBuilder<T>` / `conduit::event(T)`          | `conduit/builder.hpp`               | Fluent builder for envelopes.                                             |
| `conduit::Bus`                                            | `conduit/bus.hpp`                   | Dispatch root; owns transports, middleware, listeners.                    |
| `conduit::Transport`                                      | `conduit/transport.hpp`             | Abstract transport base; returns `Local` / `Remote` scope.                |
| `conduit::local::Transport`                               | `conduit/local/transport.hpp`       | In-process delivery (`Direct` / `Queue` / `ThreadPool`).                  |
| `conduit::relay::Transport`                               | `conduit/relay/transport.hpp`       | Callback transport, glob-routed.                                          |
| `conduit::FilteredTransport`                              | `conduit/filtered_transport.hpp`    | Bidirectional outbound/inbound filter wrapper.                            |
| `conduit::mqtt::Transport` (etc.)                         | `conduit/mqtt/transport.hpp` (etc.) | Broker adapters — opt-in via CMake flags.                                 |
| `conduit::Middleware`                                     | `conduit/middleware.hpp`            | `before_dispatch` / `after_dispatch` / `on_error` / `on_transport_error`. |
| `conduit::EventListener<T>` / `EventSubscriber`           | `conduit/listener.hpp`              | Class-based listener / multi-event subscriber.                            |
| `conduit::Subscription`                                   | `conduit/listener.hpp`              | RAII unsubscribe handle.                                                  |
| `conduit::Glob`                                           | `conduit/glob.hpp`                  | `*` (within segment) / `**` (across) matcher.                             |
| `conduit::flags::Flag<"name">`, `FlagSet`, built-in flags | `conduit/flags.hpp`                 | Type-tag-based flag bitset.                                               |
| `conduit::EventRegistry`                                  | `conduit/serialization.hpp`         | Registers event types for wire decode.                                    |
| `conduit::encode_json` / `encode_cbor`                    | `conduit/serialization.hpp`         | Encode an envelope to the wire.                                           |
| `conduit::Metadata` (= `md::Metadata`), `Timestamps`      | `conduit/metadata.hpp`              | Envelope metadata (typed JSON-shaped tree) + timestamp struct.            |

Built-in flags: `Direct`, `Durable`, `Persistent`, `NoMiddleware`, `RequireAck`, `Broadcast`, `LocalOnly`, `RemoteOnly`.

## Examples

The `examples/` directory contains compact programs that map to specific concepts. Each builds as `conduit_<name>` when `CONDUIT_BUILD_EXAMPLES=ON` (the default at top level).

| Example                                | Demonstrates                                                            |
|----------------------------------------|-------------------------------------------------------------------------|
| `examples/hello.cpp`                   | Minimal: event, listen, publish.                                        |
| `examples/typed_listener.cpp`          | Listener receives the envelope instead of the payload.                  |
| `examples/subscriber.cpp`              | `EventSubscriber` wires up several listeners as one unit.               |
| `examples/pattern_listener.cpp`        | `bus.listen("order.*", ...)` glob subscription.                         |
| `examples/middleware_logging.cpp`      | A logging middleware with `before_dispatch` / `after_dispatch`.         |
| `examples/threadpool_local.cpp`        | ThreadPool execution + `bus.drain()`.                                   |
| `examples/flags_direct.cpp`            | `flags::Direct` forces inline delivery even in Queue mode.              |
| `examples/local_only_event.cpp`        | `DefaultFlags<LocalOnly>` keeps an event off remote transports.         |
| `examples/relay_to_callback.cpp`       | `relay::Transport` routes matching events to a callback.                |
| `examples/filtered_transport.cpp`      | Per-leg `FilteredTransport` predicates.                                 |
| `examples/serialization_roundtrip.cpp` | JSON + CBOR encode/decode via `EventRegistry`.                          |
| `transports/<name>/examples/*.cpp`     | Broker-specific recipes — publish/subscribe, multi-topic, queue groups. |

## Testing

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The repository ships a `Makefile` that wraps the common workflows:

```bash
make test         # configure + build + ctest in build/
make sanitize     # ASan + UBSan in build-san/
make tidy         # clang-tidy via build-tidy/
make release      # Release build + tests in build-release/
make coverage     # Clang source-based coverage + HTML report
make docs         # Doxygen HTML in build-docs/docs/html/
make mqtt         # Configure + build + test with CONDUIT_TRANSPORT_MQTT=ON
make amqp         # ...AMQP
make nats         # ...NATS
make redis        # ...Redis
make zmq          # ...ZMQ
make ci           # Pre-push gate: format-check + tidy + test + sanitize + release + every transport
make format       # clang-format -i over headers/sources/tests/examples
```

Each broker smoke test (`MqttSmoke`, `AmqpSmoke`, `NatsSmoke`, `RedisSmoke`, `ZmqSmoke`) is skipped if its environment variable is not set:

| Env var                     | Example                              |
|-----------------------------|--------------------------------------|
| `CONDUIT_MQTT_TEST_BROKER`  | `tcp://localhost:1883`               |
| `CONDUIT_AMQP_TEST_BROKER`  | `amqp://guest:guest@localhost:5672/` |
| `CONDUIT_NATS_TEST_BROKER`  | `nats://localhost:4222`              |
| `CONDUIT_REDIS_TEST_BROKER` | `tcp://localhost:6379`               |
| `CONDUIT_ZMQ_TEST_ENDPOINT` | `tcp://127.0.0.1:25557`              |

CI brings each broker up as a service container and runs the corresponding smoke test against it.

## CI

GitHub Actions runs:

- `build` — Ubuntu + macOS, Debug + Release, GCC 14 / Clang 20.
- `sanitizers` — ASan + UBSan on Ubuntu / GCC 14.
- `clang-tidy` — macOS / Homebrew LLVM.
- `format` — `clang-format-22` dry-run.
- `mqtt`, `amqp`, `nats`, `redis`, `zmq` — each boots its broker as a service container (or, for ZMQ, uses a loopback endpoint) and runs the adapter's smoke test.
- `docs` — Doxygen build, deployed to GitHub Pages.

## FAQ

**Is the core header-only?** Yes. The `conduit::conduit` CMake target is `INTERFACE`. Each broker adapter is a separate static library because it pulls in heavy C dependencies (paho, nats.c, libzmq, etc.).

**Does the bus take ownership of listeners?** It stores the handler. The returned `Subscription` is the owner of the registration — destroy it to unregister. Capture-by-reference into a handler is fine as long as the captured objects outlive the bus.

**Can I use it from multiple threads?** Yes. `Bus::publish`, `Bus::listen`, and `Bus::use_transport` / `use_middleware` all take an internal mutex. Listeners themselves may run on any thread depending on the local-transport mode; treat handler bodies as multi-threaded code unless you are in `Direct` mode.

**What is `parcel`?** A separate serialization library (`cpp-parcel`) used to encode the typed payload. Events use its `FieldsBuilder` to declare schema; `conduit` builds the envelope JSON/CBOR around it.

**Can I use it without any transport?** Yes — if no transport is attached the bus performs an inline local fan-out as a fallback. This is mostly useful for tests; for real applications attach at least `local::Transport` so the routing matrix and scope filtering run.

**How do I send the same event to two different MQTT topics?** Attach two `mqtt::Transport` instances, each with its own `Config::topic`. Wrap each in a `FilteredTransport` if you only want certain envelope names to go down each path.

**What happens when the broker disconnects mid-publish?** The dispatch attempt is caught and surfaced through `Middleware::on_error`. The reconnect policy belongs to the underlying broker client (paho, nats.c, etc.); see their docs.

**Where does retention / durability live?** In the broker adapter, not the core. `Durable` / `Persistent` / `RequireAck` are carried as flags so adapters can honor them — for MQTT that means QoS and retain; for AMQP it means `delivery_mode=2` and `confirm.select`; etc. The core promises only to *carry* the flags.

**Can I add my own transport?** Yes. Inherit from `conduit::Transport`, implement `scope()` and `dispatch(const EventEnvelopeView&)`, and optionally override `attach_with_sink`, `detach`, and `flush`. For inbound delivery, call `deliver_inbound(envelope)` from your read path; for decode failures, call `bus()->report_transport_error("my_transport", std::current_exception())`.

## Contributing

Contributions to the library are welcome! If you encounter any issues or have suggestions for
improvements,
please feel free to submit a pull request or open an issue on the project's repository.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
