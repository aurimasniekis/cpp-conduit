/// @file
/// @brief Implementation of `conduit::redis::Transport` backed by
///        redis-plus-plus (and hiredis under it).

#include <conduit/bus.hpp>
#include <conduit/envelope.hpp>
#include <conduit/redis/transport.hpp>
#include <conduit/serialization.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <sw/redis++/redis++.h>
#include <sw/redis++/redis_uri.h>

namespace conduit::redis {

namespace {

/// Translate the conduit Config into a redis-plus-plus ConnectionOptions.
/// Accepts URLs of the form `tcp://host:port`, `redis://host:port`, or
/// `tls://host:port` (the last is normalised to redis-plus-plus's `rediss://`).
[[nodiscard]] sw::redis::ConnectionOptions to_connection_options(const Config& c) {
    std::string url = c.url;
    if (url.starts_with("tls://")) {
        url.replace(0, std::string_view{"tls://"}.size(), "rediss://");
    }
    const sw::redis::Uri parsed{url};
    sw::redis::ConnectionOptions opts = parsed.connection_options();
    if (c.username) {
        opts.user = *c.username;
    }
    if (c.password) {
        opts.password = *c.password;
    }
    if (c.db) {
        opts.db = *c.db;
    }
    opts.connect_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(c.connect_timeout);
#ifdef REDIS_PLUS_PLUS_USE_TLS
    if (c.tls) {
        opts.tls.enabled = true;
        opts.tls.cacert = c.tls->ca_file;
        if (c.tls->cert_file) {
            opts.tls.cert = *c.tls->cert_file;
        }
        if (c.tls->key_file) {
            opts.tls.key = *c.tls->key_file;
        }
    }
#else
    if (c.tls) {
        throw TlsNotSupportedError{
            "conduit::redis::Transport: Config::tls set but the transport was "
            "built without CONDUIT_TRANSPORT_REDIS_TLS"};
    }
#endif
    return opts;
}

constexpr std::chrono::milliseconds k_subscriber_poll_timeout{200};

}  // namespace

struct Transport::Impl {
    Config config;
    std::shared_ptr<EventRegistry> registry;
    InboundSink sink;
    std::function<void(const std::exception_ptr&)> error_sink;

    std::unique_ptr<sw::redis::Redis> publisher;
    std::unique_ptr<sw::redis::Redis>
        subscriber_conn;  // owns the subscriber's underlying connection
    std::unique_ptr<sw::redis::Subscriber> subscriber;
    std::thread subscriber_thread;
    std::atomic<bool> stop{false};
    std::atomic<bool> connected{false};

    Impl(Config cfg, std::shared_ptr<EventRegistry> reg)
        : config(std::move(cfg)), registry(std::move(reg)) {
        if (config.channel.empty()) {
            throw ConfigError("conduit::redis::Transport: Config::channel must be non-empty");
        }
    }

    void shutdown() noexcept {
        stop.store(true, std::memory_order_release);
        if (subscriber_thread.joinable()) {
            try {
                subscriber_thread.join();
            } catch (...) {
                // ignore — we're shutting down
            }
        }
        subscriber.reset();
        subscriber_conn.reset();
        publisher.reset();
        connected.store(false, std::memory_order_release);
    }

    void deliver(std::string_view channel, std::string_view payload) const {
        (void)channel;
        if (!sink || !registry) {
            return;
        }
        try {
            EventEnvelope v;
            if (config.format == Format::Cbor) {
                v = registry->decode_cbor(std::span<const char>{payload.data(), payload.size()});
            } else {
                auto j = nlohmann::json::parse(payload);
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
            b->report_transport_error("redis", ep);
        }
    };

    try {
        auto pub_opts = to_connection_options(impl_->config);
        impl_->publisher = std::make_unique<sw::redis::Redis>(pub_opts);
        impl_->publisher->ping();  // force a round-trip so failures surface now

        auto sub_opts = to_connection_options(impl_->config);
        // Poll the subscriber socket on this cadence so consume() unblocks
        // regularly and the loop can observe the stop flag.
        sub_opts.socket_timeout = k_subscriber_poll_timeout;
        impl_->subscriber_conn = std::make_unique<sw::redis::Redis>(sub_opts);

        auto sub = std::make_unique<sw::redis::Subscriber>(impl_->subscriber_conn->subscriber());
        sub->on_message([this](const std::string& channel, const std::string& payload) {
            impl_->deliver(channel, payload);
        });
        sub->subscribe(impl_->config.channel);
        impl_->subscriber = std::move(sub);
        impl_->connected.store(true, std::memory_order_release);
    } catch (const std::exception& e) {
        impl_->shutdown();
        throw RedisError{std::string{"conduit::redis::Transport::attach: "} + e.what()};
    }

    impl_->stop.store(false, std::memory_order_release);
    impl_->subscriber_thread = std::thread([this]() {
        while (!impl_->stop.load(std::memory_order_acquire)) {
            try {
                impl_->subscriber->consume();
            } catch (const sw::redis::TimeoutError&) {
                continue;  // expected — socket_timeout fired, recheck stop flag
            } catch (...) {
                // connection dropped or subscriber destroyed — exit cleanly
                break;
            }
        }
    });
}

void Transport::detach() noexcept {
    if (!impl_) {
        return;
    }
    impl_->shutdown();
    impl_->sink = nullptr;
}

void Transport::dispatch(const EventEnvelopeView& v) {
    if (!impl_ || !impl_->publisher) {
        return;
    }

    std::string payload;
    if (impl_->config.format == Format::Json) {
        payload = encode_json(v).dump();
    } else {
        const auto bytes = encode_cbor(v);
        payload.assign(bytes.data(), bytes.size());
    }

    try {
        const auto subscriber_count = impl_->publisher->publish(impl_->config.channel, payload);
        (void)subscriber_count;
        // RequireAck: a 0 subscriber count means no one received the message.
        // We don't throw — Redis pub/sub is fire-and-forget by design; a
        // follow-up middleware hook will surface this via on_error.
    } catch (...) {
        // best-effort, mirrors MQTT
    }
}

void Transport::flush() {
    // No-op: sw::redis::Redis::publish() is synchronous.
}

bool Transport::is_connected() const noexcept {
    return impl_ && impl_->connected.load(std::memory_order_acquire);
}

}  // namespace conduit::redis
