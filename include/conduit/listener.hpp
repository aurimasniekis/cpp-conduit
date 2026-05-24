#pragma once

/// @file
/// @brief Listener / Subscription / Subscriber primitives.

#include <conduit/envelope.hpp>
#include <conduit/event.hpp>

#include <concepts>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <parcel/parcel.h>

namespace conduit {

class Bus;

namespace detail {

/// Token used by the bus to unregister a listener.
using SubscriptionId = std::uint64_t;

/// Erased back-reference the Subscription holds. The bus implements this so
/// that destruction of the subscription handle is decoupled from the bus type
/// (no forward-declaration of internal containers needed at this point).
class SubscriptionBackref {
public:
    SubscriptionBackref() = default;
    SubscriptionBackref(const SubscriptionBackref&) = default;
    SubscriptionBackref(SubscriptionBackref&&) noexcept = default;
    SubscriptionBackref& operator=(const SubscriptionBackref&) = default;
    SubscriptionBackref& operator=(SubscriptionBackref&&) noexcept = default;
    virtual ~SubscriptionBackref() = default;
    virtual void release(SubscriptionId id) noexcept = 0;
};

}  // namespace detail

/// RAII handle returned by `Bus::listen(...)`. On destruction (or `release()`),
/// it unregisters the listener from the owning bus.
class Subscription {
public:
    Subscription() noexcept = default;

    Subscription(std::shared_ptr<detail::SubscriptionBackref> backref,
                 const detail::SubscriptionId id) noexcept
        : backref_(std::move(backref)), id_(id), active_(true) {}

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    Subscription(Subscription&& other) noexcept
        : backref_(std::move(other.backref_)), id_(other.id_), active_(other.active_) {
        other.active_ = false;
    }
    Subscription& operator=(Subscription&& other) noexcept {
        if (this != &other) {
            release();
            backref_ = std::move(other.backref_);
            id_ = other.id_;
            active_ = other.active_;
            other.active_ = false;
        }
        return *this;
    }

    ~Subscription() {
        release();
    }

    void release() noexcept {
        if (active_ && backref_) {
            backref_->release(id_);
        }
        active_ = false;
        backref_.reset();
    }

    void detach() noexcept {
        active_ = false;
        backref_.reset();
    }

    [[nodiscard]] bool active() const noexcept {
        return active_;
    }

private:
    std::shared_ptr<detail::SubscriptionBackref> backref_;
    detail::SubscriptionId id_ = 0;
    bool active_ = false;
};

/// Class-based listener — derive and override `on_event`.
template <typename T>
class EventListener {
public:
    static_assert(std::derived_from<T, parcel::ICell>,
                  "EventListener<T>: T must derive from conduit::Event<T, Name>");

    EventListener() = default;
    EventListener(const EventListener&) = default;
    EventListener(EventListener&&) noexcept = default;
    EventListener& operator=(const EventListener&) = default;
    EventListener& operator=(EventListener&&) noexcept = default;
    virtual ~EventListener() = default;

    virtual void on_event(const T&) = 0;
};

/// Multi-event subscriber base. Override `register_to(Bus&)` and use the
/// `on(...)` helpers to register handlers; the produced subscriptions live
/// on the subscriber for as long as it does.
class EventSubscriber {
public:
    EventSubscriber() = default;
    EventSubscriber(const EventSubscriber&) = delete;
    EventSubscriber(EventSubscriber&&) noexcept = default;
    EventSubscriber& operator=(const EventSubscriber&) = delete;
    EventSubscriber& operator=(EventSubscriber&&) noexcept = default;
    virtual ~EventSubscriber() = default;

    virtual void register_to(Bus& bus) = 0;

    [[nodiscard]] std::vector<Subscription>& subscriptions() noexcept {
        return subscriptions_;
    }

protected:
    template <typename T, typename F>
    void on(Bus& bus, F&& handler);

    template <typename F>
    void on(Bus& bus, std::string_view pattern, F&& handler);

private:
    std::vector<Subscription> subscriptions_;
};

}  // namespace conduit
