/// @file
/// @brief Implementation of `conduit::zmq::Transport` backed by libzmq + cppzmq.

#include <conduit/bus.hpp>
#include <conduit/envelope.hpp>
#include <conduit/serialization.hpp>
#include <conduit/zmq/transport.hpp>

#include <threadman/manager.hpp>
#include <zmq.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace conduit::zmq {

namespace {

constexpr auto k_shutdown_endpoint = "inproc://conduit-zmq-shutdown";

[[nodiscard]] ::zmq::socket_type outbound_type(const Pattern p) {
    switch (p) {
    case Pattern::PubSub:
        return ::zmq::socket_type::pub;
    case Pattern::PushPull:
        return ::zmq::socket_type::push;
    case Pattern::RouterDealer:
        return ::zmq::socket_type::dealer;
    }
    return ::zmq::socket_type::pub;
}

[[nodiscard]] ::zmq::socket_type inbound_type(const Pattern p) {
    switch (p) {
    case Pattern::PubSub:
        return ::zmq::socket_type::sub;
    case Pattern::PushPull:
        return ::zmq::socket_type::pull;
    case Pattern::RouterDealer:
        return ::zmq::socket_type::router;
    }
    return ::zmq::socket_type::sub;
}

void bind_or_connect(::zmq::socket_t& sock, const std::string& endpoint, const Role role) {
    if (role == Role::Bind) {
        sock.bind(endpoint);
    } else {
        sock.connect(endpoint);
    }
}

void validate(const Config& c) {
    switch (c.pattern) {
    case Pattern::PubSub:
        if (c.pub_endpoint.empty() && c.sub_endpoint.empty()) {
            throw ConfigError(
                "conduit::zmq::Transport: PubSub requires pub_endpoint or sub_endpoint");
        }
        break;
    case Pattern::PushPull:
        if (c.push_endpoint.empty() && c.pull_endpoint.empty()) {
            throw ConfigError(
                "conduit::zmq::Transport: PushPull requires push_endpoint or pull_endpoint");
        }
        break;
    case Pattern::RouterDealer:
        if (c.endpoint.empty()) {
            throw ConfigError("conduit::zmq::Transport: RouterDealer requires endpoint");
        }
        break;
    }
}

}  // namespace

struct Transport::Impl {
    Config config;
    std::shared_ptr<EventRegistry> registry;
    InboundSink sink;
    std::function<void(const std::exception_ptr&)> error_sink;

    std::unique_ptr<::zmq::context_t> context;
    std::unique_ptr<::zmq::socket_t> outbound;
    std::unique_ptr<::zmq::socket_t> inbound;
    std::unique_ptr<::zmq::socket_t> shutdown_pub;
    std::unique_ptr<::zmq::socket_t> shutdown_sub;
    std::unique_ptr<threadman::ManagedThread> worker;
    std::atomic<bool> connected{false};

    Impl(Config cfg, std::shared_ptr<EventRegistry> reg)
        : config(std::move(cfg)), registry(std::move(reg)) {
        validate(config);
#ifndef CONDUIT_ZMQ_HAS_CURVE
        if (config.curve) {
            throw ConfigError("conduit::zmq::Transport: Config::curve set but the transport was "
                              "built without CONDUIT_TRANSPORT_ZMQ_CURVE");
        }
#endif
    }

    void apply_socket_opts(::zmq::socket_t& sock, const bool is_outbound) const {
        const int linger_ms = static_cast<int>(config.linger.count());
        sock.set(::zmq::sockopt::linger, linger_ms);
        if (is_outbound) {
            sock.set(::zmq::sockopt::sndhwm, config.send_hwm);
        } else {
            sock.set(::zmq::sockopt::rcvhwm, config.recv_hwm);
        }
        if (config.identity) {
            sock.set(::zmq::sockopt::routing_id, *config.identity);
        }
#ifdef CONDUIT_ZMQ_HAS_CURVE
        if (config.curve) {
            const auto& cv = *config.curve;
            if (cv.is_server) {
                sock.set(::zmq::sockopt::curve_server, 1);
                sock.set(::zmq::sockopt::curve_secretkey, cv.secret_key);
            } else {
                sock.set(::zmq::sockopt::curve_serverkey, cv.server_key);
                sock.set(::zmq::sockopt::curve_publickey, cv.public_key);
                sock.set(::zmq::sockopt::curve_secretkey, cv.secret_key);
            }
        }
#endif
    }

    void shutdown() noexcept {
        if (worker) {
            worker->request_stop();
        }
        if (shutdown_pub) {
            try {
                constexpr char wake = 'x';
                shutdown_pub->send(::zmq::buffer(&wake, 1), ::zmq::send_flags::dontwait);
            } catch (...) {}
        }
        if (worker) {
            try {
                if (worker->joinable()) {
                    worker->join();
                }
            } catch (...) {}
            worker.reset();
        }
        try {
            inbound.reset();
        } catch (...) {}
        try {
            outbound.reset();
        } catch (...) {}
        try {
            shutdown_pub.reset();
        } catch (...) {}
        try {
            shutdown_sub.reset();
        } catch (...) {}
        try {
            context.reset();
        } catch (...) {}
        connected.store(false, std::memory_order_release);
    }

    void deliver_bytes(const void* data, std::size_t len) const {
        if (!sink || !registry || data == nullptr || len == 0) {
            return;
        }
        try {
            EventEnvelope v;
            if (config.format == Format::Cbor) {
                std::span<const std::uint8_t> bytes{static_cast<const std::uint8_t*>(data), len};
                v = registry->decode_cbor(bytes);
            } else {
                auto j =
                    nlohmann::json::parse(std::string_view{static_cast<const char*>(data), len});
                v = registry->decode_json(j);
            }
            v.timestamps().received_at = std::chrono::system_clock::now();
            sink(v);
        } catch (...) {
            if (error_sink) {
                error_sink(std::current_exception());
            }
        }
    }

    void run_loop(const std::stop_token& tok) const {
        while (!tok.stop_requested()) {
            std::array<::zmq::pollitem_t, 2> items = {{
                {static_cast<void*>(*inbound), 0, ZMQ_POLLIN, 0},
                {static_cast<void*>(*shutdown_sub), 0, ZMQ_POLLIN, 0},
            }};
            try {
                ::zmq::poll(items.data(), items.size(), std::chrono::milliseconds(200));
            } catch (...) {
                break;
            }
            if ((items[1].revents & ZMQ_POLLIN) != 0) {
                try {
                    ::zmq::message_t drain;
                    (void)shutdown_sub->recv(drain, ::zmq::recv_flags::dontwait);
                } catch (...) {}
                break;
            }
            if ((items[0].revents & ZMQ_POLLIN) != 0) {
                try {
                    if (config.pattern == Pattern::RouterDealer) {
                        // ROUTER prefixes the routing-id frame; receive the
                        // first part, then read the payload frame.
                        ::zmq::message_t prefix;
                        if (const auto got = inbound->recv(prefix, ::zmq::recv_flags::dontwait);
                            !got || !prefix.more()) {
                            continue;
                        }
                        ::zmq::message_t payload;
                        if (const auto got2 = inbound->recv(payload, ::zmq::recv_flags::dontwait)) {
                            deliver_bytes(payload.data(), payload.size());
                        }
                    } else {
                        ::zmq::message_t msg;
                        if (const auto got = inbound->recv(msg, ::zmq::recv_flags::dontwait)) {
                            deliver_bytes(msg.data(), msg.size());
                        }
                    }
                } catch (...) {
                    // best-effort: drop frame
                }
            }
        }
    }
};

// -- Transport public surface ----------------------------------------------

Transport::Transport(Config config, std::shared_ptr<EventRegistry> registry)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(registry))) {}

Transport::~Transport() {
    if (impl_) {
        impl_->shutdown();
    }
}

void Transport::attach_with_sink(Bus& bus, InboundSink sink) {
    conduit::Transport::attach_with_sink(bus, std::move(sink));
    if (!impl_->registry) {
        impl_->registry = bus.registry();
    }
    impl_->sink = [this](const EventEnvelopeView& v) { this->deliver_inbound(v); };
    impl_->error_sink = [this](const std::exception_ptr& ep) {
        if (const auto* b = this->bus()) {
            b->report_transport_error("zmq", ep);
        }
    };

    try {
        impl_->context = std::make_unique<::zmq::context_t>(1);

        if (impl_->config.pattern == Pattern::RouterDealer) {
            // Single bidirectional socket; outbound writes use the same socket.
            impl_->outbound = std::make_unique<::zmq::socket_t>(
                *impl_->context, outbound_type(impl_->config.pattern));
            impl_->apply_socket_opts(*impl_->outbound, /*is_outbound=*/true);

            impl_->inbound = std::make_unique<::zmq::socket_t>(*impl_->context,
                                                               inbound_type(impl_->config.pattern));
            impl_->apply_socket_opts(*impl_->inbound, /*is_outbound=*/false);

            bind_or_connect(*impl_->outbound, impl_->config.endpoint, impl_->config.endpoint_role);
            bind_or_connect(*impl_->inbound,
                            impl_->config.endpoint,
                            impl_->config.endpoint_role == Role::Bind ? Role::Connect : Role::Bind);
        } else if (impl_->config.pattern == Pattern::PubSub) {
            if (!impl_->config.pub_endpoint.empty()) {
                impl_->outbound =
                    std::make_unique<::zmq::socket_t>(*impl_->context, ::zmq::socket_type::pub);
                impl_->apply_socket_opts(*impl_->outbound, /*is_outbound=*/true);
                bind_or_connect(
                    *impl_->outbound, impl_->config.pub_endpoint, impl_->config.pub_role);
            }
            if (!impl_->config.sub_endpoint.empty()) {
                impl_->inbound =
                    std::make_unique<::zmq::socket_t>(*impl_->context, ::zmq::socket_type::sub);
                impl_->apply_socket_opts(*impl_->inbound, /*is_outbound=*/false);
                impl_->inbound->set(::zmq::sockopt::subscribe, impl_->config.subscription_prefix);
                bind_or_connect(
                    *impl_->inbound, impl_->config.sub_endpoint, impl_->config.sub_role);
            }
        } else {  // PushPull
            if (!impl_->config.push_endpoint.empty()) {
                impl_->outbound =
                    std::make_unique<::zmq::socket_t>(*impl_->context, ::zmq::socket_type::push);
                impl_->apply_socket_opts(*impl_->outbound, /*is_outbound=*/true);
                bind_or_connect(
                    *impl_->outbound, impl_->config.push_endpoint, impl_->config.push_role);
            }
            if (!impl_->config.pull_endpoint.empty()) {
                impl_->inbound =
                    std::make_unique<::zmq::socket_t>(*impl_->context, ::zmq::socket_type::pull);
                impl_->apply_socket_opts(*impl_->inbound, /*is_outbound=*/false);
                bind_or_connect(
                    *impl_->inbound, impl_->config.pull_endpoint, impl_->config.pull_role);
            }
        }

        // Inproc shutdown PAIR — the worker thread polls this socket to
        // unblock its zmq::poll on detach.
        if (impl_->inbound) {
            impl_->shutdown_sub =
                std::make_unique<::zmq::socket_t>(*impl_->context, ::zmq::socket_type::pair);
            impl_->shutdown_sub->set(::zmq::sockopt::linger, 0);
            impl_->shutdown_sub->bind(k_shutdown_endpoint);
            impl_->shutdown_pub =
                std::make_unique<::zmq::socket_t>(*impl_->context, ::zmq::socket_type::pair);
            impl_->shutdown_pub->set(::zmq::sockopt::linger, 0);
            impl_->shutdown_pub->connect(k_shutdown_endpoint);
        }

        impl_->connected.store(true, std::memory_order_release);
    } catch (const std::exception& e) {
        impl_->shutdown();
        throw ZmqError{std::string{"conduit::zmq::Transport::attach: "} + e.what()};
    }

    if (impl_->inbound) {
        threadman::ManagedThread::Options topts;
        topts.name = "conduit:zmq-worker";
        impl_->worker = std::make_unique<threadman::ManagedThread>(
            std::move(topts), [this](const std::stop_token& tok) { impl_->run_loop(tok); });
    }
}

void Transport::detach() noexcept {
    if (!impl_) {
        return;
    }
    impl_->shutdown();
    impl_->sink = nullptr;
}

void Transport::dispatch(const EventEnvelopeView& v) {
    if (!impl_ || !impl_->outbound) {
        return;
    }

    std::vector<char> payload;
    if (impl_->config.format == Format::Json) {
        const auto str = encode_json(v).dump();
        payload.assign(str.begin(), str.end());
    } else {
        payload = encode_cbor(v);
    }

    try {
        ::zmq::message_t msg(payload.data(), payload.size());
        // Best-effort send; on EAGAIN (HWM) we drop the frame, matching the
        // best-effort semantics MQTT/Redis use.
        (void)impl_->outbound->send(msg, ::zmq::send_flags::dontwait);
    } catch (...) {
        // EAGAIN, ETERM, etc. — drop.
    }
}

void Transport::flush() {
    // No-op: ZMQ's linger socket option handles in-flight messages on close.
}

bool Transport::is_connected() const noexcept {
    return impl_ && impl_->connected.load(std::memory_order_acquire);
}

}  // namespace conduit::zmq
