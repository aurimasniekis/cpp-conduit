/// @file
/// @brief Implementation of `conduit::mqtt::Transport` backed by paho.mqtt.cpp.

#include <conduit/bus.hpp>
#include <conduit/envelope.hpp>
#include <conduit/mqtt/transport.hpp>
#include <conduit/serialization.hpp>

#include <ulid/ulid.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mqtt/async_client.h>

namespace conduit::mqtt {

struct Transport::Impl : public virtual ::mqtt::callback {
    Config config;
    std::shared_ptr<EventRegistry> registry;  // either user-supplied or bus's
    std::unique_ptr<::mqtt::async_client> client;
    InboundSink sink;
    std::function<void(const std::exception_ptr&)> error_sink;

    Impl(Config cfg, std::shared_ptr<EventRegistry> reg)
        : config(std::move(cfg)), registry(std::move(reg)) {
        if (config.topic.empty()) {
            throw std::invalid_argument(
                "conduit::mqtt::Transport: Config::topic must be non-empty");
        }
        if (config.client_id.empty()) {
            config.client_id = "conduit-" + ulid::generate().string();
        }
        client = std::make_unique<::mqtt::async_client>(config.url, config.client_id);
        client->set_callback(*this);
    }

    void connect_blocking() const {
        ::mqtt::connect_options opts;
        opts.set_clean_session(config.clean_session);
        opts.set_keep_alive_interval(static_cast<int>(config.keep_alive.count()));
        if (config.username)
            opts.set_user_name(*config.username);
        if (config.password)
            opts.set_password(*config.password);
        if (config.tls) {
            ::mqtt::ssl_options ssl;
            ssl.set_trust_store(config.tls->ca_file);
            if (config.tls->cert_file)
                ssl.set_key_store(*config.tls->cert_file);
            if (config.tls->key_file)
                ssl.set_private_key(*config.tls->key_file);
            ssl.set_verify(config.tls->verify_peer);
            opts.set_ssl(ssl);
        }
        const auto tok = client->connect(opts);
        tok->wait_for(std::chrono::milliseconds(config.connect_timeout));
    }

    void disconnect_blocking() const noexcept {
        if (!client)
            return;
        try {
            if (client->is_connected()) {
                client->disconnect()->wait_for(std::chrono::seconds(5));
            }
        } catch (...) {
            // suppress — shutdown must not throw
        }
    }

    // -- callback overrides --
    void message_arrived(::mqtt::const_message_ptr msg) override {
        if (!sink || !registry) {
            return;
        }
        try {
            EventEnvelope v;
            if (config.format == Format::Cbor) {
                const auto& payload = msg->get_payload();
                v = registry->decode_cbor(std::span<const char>{payload.data(), payload.size()});
            } else {
                auto j = nlohmann::json::parse(msg->get_payload_str());
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
        impl_->disconnect_blocking();
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
            b->report_transport_error("mqtt", ep);
        }
    };

    try {
        impl_->connect_blocking();
    } catch (const std::exception& e) {
        throw std::runtime_error{std::string{"conduit::mqtt::Transport::attach: connect failed: "} +
                                 e.what()};
    }
    if (impl_->client && impl_->client->is_connected()) {
        try {
            impl_->client->subscribe(impl_->config.topic, impl_->config.qos)
                ->wait_for(std::chrono::seconds(5));
        } catch (const std::exception& e) {
            throw std::runtime_error{
                std::string{"conduit::mqtt::Transport::attach: topic subscribe failed: "} +
                e.what()};
        }
    }
}

void Transport::detach() noexcept {
    if (!impl_)
        return;
    impl_->disconnect_blocking();
    impl_->sink = nullptr;
}

void Transport::dispatch(const EventEnvelopeView& v) {
    if (!impl_ || !impl_->client)
        return;

    std::vector<char> payload;
    if (impl_->config.format == Format::Json) {
        const auto j = encode_json(v);
        const auto str = j.dump();
        payload.assign(str.begin(), str.end());
    } else {
        payload = encode_cbor(v);
    }

    const auto msg = ::mqtt::make_message(impl_->config.topic,
                                          payload.data(),
                                          payload.size(),
                                          impl_->config.qos,
                                          /*retained=*/false);
    const auto tok = impl_->client->publish(msg);
    if (v.flags().contains<flags::RequireAck>()) {
        tok->wait_for(std::chrono::seconds(5));
    }
}

void Transport::flush() {
    if (!impl_ || !impl_->client)
        return;
    // No-op: paho's async client tracks delivery internally.
}

bool Transport::is_connected() const noexcept {
    return impl_ && impl_->client && impl_->client->is_connected();
}

}  // namespace conduit::mqtt
