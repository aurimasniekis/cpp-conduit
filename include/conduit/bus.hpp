#pragma once

/// @file
/// @brief Bus — owns transports, middleware, listeners; dispatches envelopes.

#include <conduit/builder.hpp>
#include <conduit/envelope.hpp>
#include <conduit/event.hpp>
#include <conduit/flags.hpp>
#include <conduit/glob.hpp>
#include <conduit/listener.hpp>
#include <conduit/middleware.hpp>
#include <conduit/serialization.hpp>
#include <conduit/transport.hpp>

#include <atomic>
#include <concepts>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <parcel/parcel.h>

namespace conduit {

namespace detail {

struct ListenerEntry {
    SubscriptionId id;
    std::function<void(const EventEnvelope&)> handler;
    std::string name_or_pattern;  // empty if typed
    bool is_pattern = false;
};

}  // namespace detail

class Bus : public detail::SubscriptionBackref, public std::enable_shared_from_this<Bus> {
public:
    Bus() : registry_(std::make_shared<EventRegistry>()) {}

    explicit Bus(std::shared_ptr<EventRegistry> registry)
        : registry_(registry ? std::move(registry) : std::make_shared<EventRegistry>()) {}

    ~Bus() override {
        shutdown();
    }

    Bus(const Bus&) = delete;
    Bus& operator=(const Bus&) = delete;
    Bus(Bus&&) = delete;
    Bus& operator=(Bus&&) = delete;

    // -- Transports ---------------------------------------------------------

    template <typename Tp, typename... Args>
    Tp& use_transport(Args&&... args) {
        auto t = std::make_shared<Tp>(std::forward<Args>(args)...);
        Tp& ref = *t;
        attach_transport(std::move(t));
        return ref;
    }

    void use_transport(std::shared_ptr<Transport> t) {
        attach_transport(std::move(t));
    }

    // -- Middleware ---------------------------------------------------------

    template <typename M, typename... Args>
    M& use_middleware(Args&&... args) {
        auto m = std::make_shared<M>(std::forward<Args>(args)...);
        M& ref = *m;
        std::scoped_lock lock(mu_);
        middleware_.push_back(std::move(m));
        return ref;
    }

    void use_middleware(std::shared_ptr<Middleware> m) {
        std::scoped_lock lock(mu_);
        middleware_.push_back(std::move(m));
    }

    // -- Event registry ------------------------------------------------------

    /// Shared registry handle. Transports that need to decode envelopes off
    /// the wire (MQTT, future brokered transports) share this same instance
    /// unless given their own at construction time.
    [[nodiscard]] const std::shared_ptr<EventRegistry>& registry() const noexcept {
        return registry_;
    }

    /// Register an event type `T` with the bus's event registry. `listen<T>`
    /// and typed `publish<T>` overloads call this implicitly, so most callers
    /// never invoke it directly — it's exposed for pattern-only listeners
    /// that still want a known type's descriptor registered up front.
    template <typename T>
    void register_event() {
        static_assert(std::derived_from<T, parcel::ICell>,
                      "Bus::register_event<T>(): T must derive from "
                      "conduit::Event<T, Name>");
        registry_->add_descriptor(T::descriptor());
    }

    // -- Listener registration ----------------------------------------------

    /// Typed listener; the handler is called with `const EventEnvelope&` or
    /// `const T&`, or a `shared_ptr<EventListener<T>>`.
    template <typename T, typename F>
    [[nodiscard]] Subscription listen(F&& handler) {
        static_assert(std::derived_from<T, parcel::ICell>,
                      "Bus::listen<T>(F): T must derive from conduit::Event<T, Name>");

        register_event<T>();
        auto wrapped = wrap_typed_handler<T>(std::forward<F>(handler));
        return register_listener_for_name(std::string{T::event_name_v},
                                          std::move(wrapped),
                                          /*is_pattern=*/false);
    }

    template <typename T>
    [[nodiscard]] Subscription listen(std::shared_ptr<EventListener<T>> listener) {
        register_event<T>();
        auto wrapped = [listener](const EventEnvelope& v) {
            if (auto typed = v.payload_as<T>(); typed) {
                listener->on_event(*typed);
            }
        };
        return register_listener_for_name(std::string{T::event_name_v},
                                          std::move(wrapped),
                                          /*is_pattern=*/false);
    }

    /// Runtime listener by exact name or glob pattern.
    template <typename F>
    [[nodiscard]] Subscription listen(const std::string_view pattern, F&& handler) {
        const bool is_pattern = (pattern.contains('*'));
        std::function<void(const EventEnvelope&)> wrapped =
            [h = std::forward<F>(handler)](const EventEnvelope& v) mutable { h(v); };
        return register_listener_for_name(std::string{pattern}, std::move(wrapped), is_pattern);
    }

    // -- Subscribers --------------------------------------------------------

    void register_subscriber(EventSubscriber& s) {
        s.register_to(*this);
    }

    // -- Publish ------------------------------------------------------------

    void publish(const EventEnvelope& env) {
        publish_impl(env);
    }

    template <typename T>
    void publish(EventBuilder<T>&& b) {
        register_event<T>();
        publish_impl(std::move(b).build());
    }

    template <typename T>
    void publish(EventBuilder<T>& b) {
        register_event<T>();
        publish_impl(b.build());
    }

    template <typename T>
    auto publish(T payload) -> void
        requires(std::is_base_of_v<parcel::ICell, T>)
    {
        register_event<T>();
        publish_impl(event(std::move(payload)).build());
    }

    // -- Lifecycle ----------------------------------------------------------

    void drain() const {
        std::vector<std::shared_ptr<Transport>> tcopy;
        {
            std::scoped_lock lock(mu_);
            tcopy = transports_;
        }
        for (const auto& t : tcopy) {
            t->flush();
        }
    }

    void shutdown() noexcept {
        if (shutdown_.exchange(true)) {
            return;
        }
        std::vector<std::shared_ptr<Transport>> tcopy;
        {
            std::scoped_lock lock(mu_);
            tcopy = std::move(transports_);
            transports_.clear();
        }
        for (const auto& t : tcopy) {
            try {
                t->flush();
            } catch (...) {
                // ignore — shutdown must not throw
            }
            try {
                t->detach();
            } catch (...) {
                // ignore — shutdown must not throw
            }
        }
    }

    /// Called by transports (e.g. local::Transport) to perform the actual
    /// listener fan-out for an envelope. Public so transports can invoke it
    /// after scheduling onto their own executor.
    void deliver_to_listeners(const EventEnvelope& v) const {
        std::vector<detail::ListenerEntry> snapshot;
        {
            std::scoped_lock lock(mu_);
            snapshot = listeners_;
        }
        const std::string_view name = v.name();
        for (const auto& e : snapshot) {
            const bool match =
                e.is_pattern ? Glob::match(e.name_or_pattern, name) : (e.name_or_pattern == name);
            if (!match) {
                continue;
            }
            try {
                e.handler(v);
            } catch (...) {
                run_on_error(v, std::current_exception());
            }
        }
    }

    /// Surface a transport-level failure (e.g. an inbound message that failed
    /// to decode) through the middleware pipeline. Called by transport
    /// adapters from their inbound paths; user code rarely invokes this
    /// directly.
    void report_transport_error(std::string_view transport,
                                const std::exception_ptr& ep) const noexcept {
        std::vector<std::shared_ptr<Middleware>> snap;
        {
            std::scoped_lock lock(mu_);
            snap = middleware_;
        }
        for (const auto& m : snap) {
            try {
                m->on_transport_error(transport, ep);
            } catch (...) {
                // ignore — error sink must be silent
            }
        }
    }

    /// SubscriptionBackref hook.
    void release(const detail::SubscriptionId id) noexcept override {
        std::scoped_lock lock(mu_);
        for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
            if (it->id == id) {
                listeners_.erase(it);
                return;
            }
        }
    }

private:
    void attach_transport(std::shared_ptr<Transport> t) {
        t->attach(*this);
        std::scoped_lock lock(mu_);
        transports_.push_back(std::move(t));
    }

    template <typename T, typename F>
    static std::function<void(const EventEnvelope&)> wrap_typed_handler(F&& handler) {
        using Handler = std::decay_t<F>;
        if constexpr (std::is_invocable_v<Handler, const T&>) {
            return [h = std::forward<F>(handler)](const EventEnvelope& v) mutable {
                if (auto typed = v.payload_as<T>(); typed) {
                    h(*typed);
                }
            };
        } else if constexpr (std::is_invocable_v<Handler, const EventEnvelope&>) {
            return [h = std::forward<F>(handler)](const EventEnvelope& v) mutable { h(v); };
        } else if constexpr (requires(Handler h, const T& t) { h->on_event(t); }) {
            return [h = std::forward<F>(handler)](const EventEnvelope& v) mutable {
                if (auto typed = v.payload_as<T>(); typed) {
                    h->on_event(*typed);
                }
            };
        } else {
            static_assert(sizeof(Handler) == 0,
                          "Bus::listen<T>(F): handler must take (const T&) or "
                          "(const EventEnvelope&), or be a pointer-like "
                          "EventListener<T>");
            return {};
        }
    }

    Subscription register_listener_for_name(std::string name_or_pattern,
                                            std::function<void(const EventEnvelope&)> handler,
                                            const bool is_pattern) {
        const auto id = next_id_.fetch_add(1, std::memory_order_relaxed) + 1;
        {
            std::scoped_lock lock(mu_);
            listeners_.push_back(detail::ListenerEntry{
                id, std::move(handler), std::move(name_or_pattern), is_pattern});
        }
        return Subscription{
            std::static_pointer_cast<detail::SubscriptionBackref>(shared_from_this_safe()), id};
    }

    std::shared_ptr<Bus> shared_from_this_safe() {
        // The bus may not yet be owned by a shared_ptr if the user constructed
        // it as a stack/local. To support both, lazily create an alias-tracker
        // that points to `this` but never deletes it.
        if (!self_alias_) {
            self_alias_ = std::shared_ptr<Bus>(this, [](Bus*) {});
        }
        return self_alias_;
    }

    void publish_impl(const EventEnvelope& v) const {
        EventEnvelope local = v;  // shared core/payload — cheap

        const bool local_only = local.flags().has<flags::LocalOnly>();
        const bool remote_only = local.flags().has<flags::RemoteOnly>();

        local.timestamps().published_at = std::chrono::system_clock::now();

        // Middleware before_dispatch — unless NoMiddleware.
        std::vector<std::shared_ptr<Middleware>> mw_snapshot;
        const bool run_mw = !local.flags().has<flags::NoMiddleware>();
        if (run_mw) {
            std::scoped_lock lock(mu_);
            mw_snapshot = middleware_;
        }

        bool proceed = true;
        if (run_mw) {
            for (const auto& m : mw_snapshot) {
                try {
                    if (!m->before_dispatch(local)) {
                        proceed = false;
                        break;
                    }
                } catch (...) {
                    try {
                        m->on_error(local, std::current_exception());
                    } catch (...) {
                        // suppress — middleware errors must not propagate
                    }
                }
            }
        }

        std::vector<std::shared_ptr<Transport>> transports_snapshot;
        {
            std::scoped_lock lock(mu_);
            transports_snapshot = transports_;
        }

        if (local_only && remote_only) {
            run_on_error(local,
                         std::make_exception_ptr(std::runtime_error(
                             "LocalOnly + RemoteOnly conflict — envelope dropped")));
            proceed = false;
        }

        if (proceed) {
            bool routed = false;
            for (const auto& t : transports_snapshot) {
                const auto sc = t->scope();
                if (local_only && sc != TransportScope::Local)
                    continue;
                if (remote_only && sc != TransportScope::Remote)
                    continue;
                try {
                    t->dispatch(local);
                    routed = true;
                } catch (...) {
                    run_on_error(local, std::current_exception());
                }
            }

            // Default: if no transport was registered, perform local fan-out inline.
            if (transports_snapshot.empty() && !remote_only) {
                deliver_to_listeners(local);
                routed = true;
            }
            (void)routed;
        }

        if (run_mw) {
            for (auto it = mw_snapshot.rbegin(); it != mw_snapshot.rend(); ++it) {
                try {
                    (*it)->after_dispatch(local);
                } catch (...) {
                    try {
                        (*it)->on_error(local, std::current_exception());
                    } catch (...) {}
                }
            }
        }
    }

    void run_on_error(const EventEnvelope& v, const std::exception_ptr& ep) const noexcept {
        std::vector<std::shared_ptr<Middleware>> snap;
        {
            std::scoped_lock lock(mu_);
            snap = middleware_;
        }
        EventEnvelope mv = v;
        for (const auto& m : snap) {
            try {
                m->on_error(mv, ep);
            } catch (...) {
                // ignore — error sink must be silent
            }
        }
    }

    mutable std::mutex mu_;
    std::shared_ptr<EventRegistry> registry_;
    std::vector<std::shared_ptr<Transport>> transports_;
    std::vector<std::shared_ptr<Middleware>> middleware_;
    std::vector<detail::ListenerEntry> listeners_;
    std::atomic<detail::SubscriptionId> next_id_{0};
    std::atomic<bool> shutdown_{false};
    std::shared_ptr<Bus> self_alias_;
};

// -- Transport base-class definitions that depend on Bus -------------------

inline void Transport::attach(Bus& bus) {
    attach_with_sink(bus, [&bus](const EventEnvelopeView& v) { bus.deliver_to_listeners(v); });
}

// -- EventSubscriber helpers (need Bus interface) --------------------------

template <typename T, typename F>
inline void EventSubscriber::on(Bus& bus, F&& handler) {
    subscriptions_.push_back(bus.listen<T>(std::forward<F>(handler)));
}

template <typename F>
inline void EventSubscriber::on(Bus& bus, std::string_view pattern, F&& handler) {
    subscriptions_.push_back(bus.listen(pattern, std::forward<F>(handler)));
}

}  // namespace conduit
