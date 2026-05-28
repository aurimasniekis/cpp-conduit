/// @file
/// @brief Implementation of `conduit::nats::Transport` backed by nats.c.

#include <conduit/bus.hpp>
#include <conduit/envelope.hpp>
#include <conduit/nats/transport.hpp>
#include <conduit/serialization.hpp>

#include <ulid/ulid.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nats.h>

namespace conduit::nats {

namespace {

[[nodiscard]] std::string nats_status_string(const natsStatus s) {
    const char* txt = natsStatus_GetText(s);
    return txt != nullptr ? std::string{txt} : std::string{"unknown"};
}

}  // namespace

struct Transport::Impl {
    Config config;
    std::shared_ptr<EventRegistry> registry;
    InboundSink sink;
    std::function<void(const std::exception_ptr&)> error_sink;

    natsOptions* opts = nullptr;
    natsConnection* conn = nullptr;
    natsSubscription* sub = nullptr;
    std::atomic<bool> connected{false};

    Impl(Config cfg, std::shared_ptr<EventRegistry> reg)
        : config(std::move(cfg)), registry(std::move(reg)) {
        if (config.subject.empty()) {
            throw ConfigError("conduit::nats::Transport: Config::subject must be non-empty");
        }
        if (config.name.empty()) {
            config.name = "conduit-" + ulid::generate().string();
        }
    }

    void shutdown() noexcept {
        if (sub != nullptr) {
            try {
                natsSubscription_Unsubscribe(sub);
            } catch (...) {}
            natsSubscription_Destroy(sub);
            sub = nullptr;
        }
        if (conn != nullptr) {
            try {
                natsConnection_Drain(conn);
            } catch (...) {}
            natsConnection_Destroy(conn);
            conn = nullptr;
        }
        if (opts != nullptr) {
            natsOptions_Destroy(opts);
            opts = nullptr;
        }
        connected.store(false, std::memory_order_release);
    }

    void deliver_bytes(const char* data, int len) const {
        if (!sink || !registry || data == nullptr || len <= 0) {
            return;
        }
        try {
            EventEnvelope v;
            if (config.format == Format::Cbor) {
                v = registry->decode_cbor(
                    std::span<const char>{data, static_cast<std::size_t>(len)});
            } else {
                auto j =
                    nlohmann::json::parse(std::string_view{data, static_cast<std::size_t>(len)});
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

    static void msg_handler(natsConnection* /*nc*/,
                            natsSubscription* /*subscription*/,
                            natsMsg* msg,
                            void* closure) {
        if (const auto* self = static_cast<Impl*>(closure); self != nullptr && msg != nullptr) {
            const char* data = natsMsg_GetData(msg);
            const int len = natsMsg_GetDataLength(msg);
            try {
                self->deliver_bytes(data, len);
            } catch (...) {}
        }
        natsMsg_Destroy(msg);
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
            b->report_transport_error("nats", ep);
        }
    };

    natsStatus s = NATS_OK;
    s = natsOptions_Create(&impl_->opts);
    if (s != NATS_OK || impl_->opts == nullptr) {
        impl_->shutdown();
        throw NatsError{"conduit::nats::Transport::attach: natsOptions_Create failed: " +
                        nats_status_string(s)};
    }

    s = natsOptions_SetURL(impl_->opts, impl_->config.url.c_str());

    if (s == NATS_OK) {
        s = natsOptions_SetName(impl_->opts, impl_->config.name.c_str());
    }
    if (s == NATS_OK) {
        s = natsOptions_SetTimeout(
            impl_->opts, static_cast<int64_t>(impl_->config.connect_timeout.count()) * 1000);
    }
    if (s == NATS_OK) {
        s = natsOptions_SetReconnectWait(
            impl_->opts, static_cast<int64_t>(impl_->config.reconnect_wait.count()) * 1000);
    }
    if (s == NATS_OK) {
        s = natsOptions_SetMaxReconnect(impl_->opts, impl_->config.max_reconnects);
    }
    if (s == NATS_OK) {
        if (impl_->config.token) {
            s = natsOptions_SetToken(impl_->opts, impl_->config.token->c_str());
        } else if (impl_->config.user && impl_->config.password) {
            s = natsOptions_SetUserInfo(
                impl_->opts, impl_->config.user->c_str(), impl_->config.password->c_str());
        }
    }
    if (s == NATS_OK && impl_->config.credentials_file) {
        s = natsOptions_SetUserCredentialsFromFiles(
            impl_->opts, impl_->config.credentials_file->c_str(), nullptr);
    }
#if defined(NATS_HAS_TLS)
    if (s == NATS_OK && impl_->config.tls) {
        s = natsOptions_SetSecure(impl_->opts, true);
        if (s == NATS_OK) {
            s = natsOptions_LoadCATrustedCertificates(impl_->opts,
                                                      impl_->config.tls->ca_file.c_str());
        }
        if (s == NATS_OK && impl_->config.tls->cert_file && impl_->config.tls->key_file) {
            s = natsOptions_LoadCertificatesChain(impl_->opts,
                                                  impl_->config.tls->cert_file->c_str(),
                                                  impl_->config.tls->key_file->c_str());
        }
        if (s == NATS_OK) {
            s = natsOptions_SkipServerVerification(impl_->opts, !impl_->config.tls->verify_peer);
        }
    }
#else
    if (impl_->config.tls) {
        impl_->shutdown();
        throw TlsNotSupportedError{
            "conduit::nats::Transport: Config::tls set but the transport was "
            "built without CONDUIT_TRANSPORT_NATS_TLS"};
    }
#endif

    if (s != NATS_OK) {
        impl_->shutdown();
        throw NatsError{"conduit::nats::Transport::attach: option setup failed: " +
                        nats_status_string(s)};
    }

    s = natsConnection_Connect(&impl_->conn, impl_->opts);
    if (s != NATS_OK || impl_->conn == nullptr) {
        impl_->shutdown();
        throw NatsError{"conduit::nats::Transport::attach: connect failed: " +
                        nats_status_string(s)};
    }

    if (impl_->config.queue_group) {
        s = natsConnection_QueueSubscribe(&impl_->sub,
                                          impl_->conn,
                                          impl_->config.subject.c_str(),
                                          impl_->config.queue_group->c_str(),
                                          &Impl::msg_handler,
                                          impl_.get());
    } else {
        s = natsConnection_Subscribe(&impl_->sub,
                                     impl_->conn,
                                     impl_->config.subject.c_str(),
                                     &Impl::msg_handler,
                                     impl_.get());
    }
    if (s != NATS_OK || impl_->sub == nullptr) {
        impl_->shutdown();
        throw NatsError{"conduit::nats::Transport::attach: subscribe failed: " +
                        nats_status_string(s)};
    }

    impl_->connected.store(true, std::memory_order_release);
}

void Transport::detach() noexcept {
    if (!impl_) {
        return;
    }
    impl_->shutdown();
    impl_->sink = nullptr;
}

void Transport::dispatch(const EventEnvelopeView& v) {
    if (!impl_ || impl_->conn == nullptr) {
        return;
    }

    std::vector<char> payload;
    if (impl_->config.format == Format::Json) {
        const auto j = encode_json(v);
        const auto str = j.dump();
        payload.assign(str.begin(), str.end());
    } else {
        payload = encode_cbor(v);
    }

    const natsStatus s = natsConnection_Publish(impl_->conn,
                                                impl_->config.subject.c_str(),
                                                payload.data(),
                                                static_cast<int>(payload.size()));
    (void)s;

    if (v.flags().contains<flags::RequireAck>()) {
        natsConnection_FlushTimeout(impl_->conn, 5000);
    }
}

void Transport::flush() {
    if (!impl_ || impl_->conn == nullptr) {
        return;
    }
    natsConnection_FlushTimeout(impl_->conn, 5000);
}

bool Transport::is_connected() const noexcept {
    return impl_ && impl_->connected.load(std::memory_order_acquire);
}

}  // namespace conduit::nats
