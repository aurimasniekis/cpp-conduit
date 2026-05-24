#pragma once

/// @file
/// @brief User-pluggable callback transport: relays matching envelopes to
///        caller-supplied callbacks for shipping to an external sink.

#include <conduit/envelope.hpp>
#include <conduit/glob.hpp>
#include <conduit/transport.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace conduit::relay {

class Transport : public conduit::Transport {
public:
    using Callback = std::function<void(const EventEnvelopeView&)>;

    explicit Transport(Callback cb) {
        add_route_impl("**", std::move(cb));
    }

    Transport(const std::string_view pattern, Callback cb) {
        add_route_impl(std::string{pattern}, std::move(cb));
    }

    [[nodiscard]] TransportScope scope() const noexcept override {
        return TransportScope::Local;
    }

    void dispatch(const EventEnvelopeView& v) override {
        std::vector<Route> snapshot;
        {
            std::scoped_lock lock(mu_);
            snapshot = routes_;
        }
        const std::string_view name = v.name();
        for (const auto& r : snapshot) {
            if (Glob::match(r.pattern, name)) {
                try {
                    r.cb(v);
                } catch (...) {
                    // Callback errors are the user's problem; swallow here so the
                    // bus's fire-and-forget contract holds.
                }
            }
        }
    }

    /// Add a new route at runtime. The returned routine id can be passed to
    /// `remove_route` (the helper that lives here, not the bus's Subscription
    /// type — relay routes are owned by the transport itself).
    [[nodiscard]] std::size_t add_route(const std::string_view pattern, Callback cb) {
        return add_route_impl(std::string{pattern}, std::move(cb));
    }

    void remove_route(const std::size_t id) noexcept {
        std::scoped_lock lock(mu_);
        for (auto it = routes_.begin(); it != routes_.end(); ++it) {
            if (it->id == id) {
                routes_.erase(it);
                return;
            }
        }
    }

    [[nodiscard]] std::size_t route_count() const noexcept {
        std::scoped_lock lock(mu_);
        return routes_.size();
    }

private:
    struct Route {
        std::size_t id;
        std::string pattern;
        Callback cb;
    };

    std::size_t add_route_impl(std::string pattern, Callback cb) {
        std::scoped_lock lock(mu_);
        const auto id = ++next_id_;
        routes_.push_back(Route{id, std::move(pattern), std::move(cb)});
        return id;
    }

    mutable std::mutex mu_;
    std::vector<Route> routes_;
    std::size_t next_id_ = 0;
};

}  // namespace conduit::relay
