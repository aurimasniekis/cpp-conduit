/// @file
/// @brief Implementation of `conduit::amqp::Transport` backed by AMQP-CPP.
///
/// AMQP-CPP separates protocol parsing (`AMQP::Connection`/`AMQP::Channel`)
/// from socket IO (`AMQP::TcpConnection`/`AMQP::TcpHandler`). The shipped
/// event-loop integrations (LibBoostAsio, LibEv …) all introduce a transitive
/// dependency conduit doesn't have, so the transport implements its own
/// minimal handler over `poll()` with a self-pipe wakeup. All AMQP-CPP API
/// calls are pinned to a single internal IO thread — `dispatch()` and
/// `detach()` post lambdas onto that thread rather than touching AMQP-CPP
/// directly.

#include <conduit/amqp/transport.hpp>
#include <conduit/bus.hpp>
#include <conduit/envelope.hpp>
#include <conduit/serialization.hpp>

#include <ulid/ulid.h>

#include <threadman/manager.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <amqpcpp.h>
#include <amqpcpp/linux_tcp.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace conduit::amqp {

namespace {

void set_nonblocking(const int fd) {
    // POSIX `fcntl` is variadic by design; that's the only way to call it.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    if (const int flags = ::fcntl(fd, F_GETFL, 0); flags >= 0) {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    // NOLINTEND(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
}

}  // namespace

struct Transport::Impl final : public AMQP::TcpHandler {
    Config config;
    std::shared_ptr<EventRegistry> registry;
    InboundSink sink;
    std::function<void(const std::exception_ptr&)> error_sink;

    // IO loop bookkeeping.
    std::array<int, 2> wakeup_pipe{-1, -1};
    int amqp_fd = -1;
    int amqp_flags = 0;
    std::unique_ptr<threadman::ManagedThread> io_thread;

    // Posted-task queue (consumed by the IO thread).
    std::mutex queue_mu;
    std::deque<std::function<void()>> task_queue;

    // AMQP-CPP objects — touched only on the IO thread after attach.
    std::unique_ptr<AMQP::TcpConnection> connection;
    std::unique_ptr<AMQP::TcpChannel> channel;
    std::string bound_queue;

    // Connection lifecycle flags.
    std::atomic<bool> ready{false};
    std::atomic<bool> connected{false};
    std::atomic<bool> error_seen{false};
    std::string last_error;

    // Publisher confirms.
    bool confirms_enabled = false;
    std::uint64_t next_delivery_tag = 1;  // touched on IO thread only
    std::mutex confirms_mu;
    std::map<std::uint64_t, std::promise<bool>> pending_confirms;

    Impl(Config cfg, std::shared_ptr<EventRegistry> reg)
        : config(std::move(cfg)), registry(std::move(reg)) {
        if (config.routing_key.empty()) {
            throw ConfigError("conduit::amqp::Transport: Config::routing_key must be non-empty");
        }
        if (config.exchange_type != "topic" && config.exchange_type != "direct" &&
            config.exchange_type != "fanout" && config.exchange_type != "headers") {
            throw ConfigError("conduit::amqp::Transport: Config::exchange_type must be "
                              "'topic', 'direct', 'fanout', or 'headers'");
        }
        if (config.connection_name.empty()) {
            config.connection_name = "conduit-" + ulid::generate().string();
        }

#ifndef CONDUIT_TRANSPORT_AMQP_TLS
        if (config.tls || config.url.starts_with("amqps://")) {
            throw TlsNotSupportedError("conduit::amqp::Transport: amqps:// URLs require "
                                       "CONDUIT_TRANSPORT_AMQP_TLS=ON at build time");
        }
#endif

        if (::pipe(wakeup_pipe.data()) != 0) {
            throw AmqpError{"conduit::amqp::Transport: pipe() failed"};
        }
        set_nonblocking(wakeup_pipe[0]);
        set_nonblocking(wakeup_pipe[1]);
        confirms_enabled = config.publisher_confirms;
    }

    ~Impl() override {
        shutdown();
        if (wakeup_pipe[0] >= 0) {
            ::close(wakeup_pipe[0]);
        }
        if (wakeup_pipe[1] >= 0) {
            ::close(wakeup_pipe[1]);
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    // -- TcpHandler --------------------------------------------------------

    void monitor(AMQP::TcpConnection* /*c*/, const int fd, const int flags) override {
        amqp_fd = fd;
        amqp_flags = flags;
        // When called from within process() on the IO thread, the next poll
        // iteration will pick up the new fd/flags. From other threads (the
        // initial TcpConnection construction is always done on the IO thread
        // in this transport, so this should not normally happen), wake.
    }

    void onReady(AMQP::TcpConnection* /*c*/) override {
        ready.store(true, std::memory_order_release);
    }

    void onConnected(AMQP::TcpConnection* /*c*/) override {
        connected.store(true, std::memory_order_release);
    }

    void onError(AMQP::TcpConnection* /*c*/, const char* message) override {
        error_seen.store(true, std::memory_order_release);
        if (message != nullptr) {
            last_error = message;
        }
        ready.store(true, std::memory_order_release);  // unblock waiters
        connected.store(false, std::memory_order_release);

        // Fail any pending publisher confirms so dispatch() callers don't hang.
        std::scoped_lock lock(confirms_mu);
        for (auto& p : pending_confirms | std::views::values) {
            try {
                p.set_value(false);
            } catch (...) {
                // promise may already be satisfied; ignore
            }
        }
        pending_confirms.clear();
    }

    void onClosed(AMQP::TcpConnection* /*c*/) override {
        connected.store(false, std::memory_order_release);
    }

    void onLost(AMQP::TcpConnection* /*c*/) override {
        connected.store(false, std::memory_order_release);
    }

    std::uint16_t onNegotiate(AMQP::TcpConnection* /*c*/, std::uint16_t /*interval*/) override {
        return static_cast<std::uint16_t>(config.heartbeat.count());
    }

    void onHeartbeat(AMQP::TcpConnection* c) override {
        if (c != nullptr) {
            c->heartbeat();
        }
    }

    // -- IO thread plumbing ------------------------------------------------

    void wake() noexcept {
        if (wakeup_pipe[1] < 0) {
            return;
        }
        const char byte = 'x';
        ::ssize_t n = 0;
        do {
            n = ::write(wakeup_pipe[1], &byte, 1);
        } while (n < 0 && errno == EINTR);
    }

    void post(std::function<void()> task) {
        {
            std::scoped_lock lock(queue_mu);
            task_queue.push_back(std::move(task));
        }
        wake();
    }

    void drain_tasks() {
        std::deque<std::function<void()>> drained;
        {
            std::scoped_lock lock(queue_mu);
            drained.swap(task_queue);
        }
        for (auto& t : drained) {
            try {
                t();
            } catch (...) {
                // tasks must not propagate exceptions into the IO loop
            }
        }
    }

    void drain_wakeup_pipe() const noexcept {
        std::array<char, 64> buf{};
        while (::read(wakeup_pipe[0], buf.data(), buf.size()) > 0) {
            // discard
        }
    }

    void io_loop(const std::stop_token& tok) {
        while (!tok.stop_requested()) {
            drain_tasks();
            if (tok.stop_requested()) {
                break;
            }

            std::array<::pollfd, 2> fds{};
            int nfds = 1;
            fds[0].fd = wakeup_pipe[0];
            fds[0].events = POLLIN;
            fds[0].revents = 0;
            if (amqp_fd >= 0 && amqp_flags != 0) {
                fds[1].fd = amqp_fd;
                fds[1].events = 0;
                if ((amqp_flags & AMQP::readable) != 0) {
                    fds[1].events |= POLLIN;
                }
                if ((amqp_flags & AMQP::writable) != 0) {
                    fds[1].events |= POLLOUT;
                }
                fds[1].revents = 0;
                nfds = 2;
            }

            const int rc = ::poll(fds.data(), static_cast<nfds_t>(nfds), 200);
            if (rc < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (rc == 0) {
                continue;
            }
            if ((fds[0].revents & POLLIN) != 0) {
                drain_wakeup_pipe();
            }
            if (nfds == 2 && fds[1].revents != 0 && connection) {
                int proc_flags = 0;
                if ((fds[1].revents & (POLLIN | POLLERR | POLLHUP)) != 0) {
                    proc_flags |= AMQP::readable;
                }
                if ((fds[1].revents & POLLOUT) != 0) {
                    proc_flags |= AMQP::writable;
                }
                try {
                    connection->process(amqp_fd, proc_flags);
                } catch (...) {
                    // AMQP-CPP process() can throw on protocol errors; record
                    // and let onError flip flags.
                }
            }
        }
    }

    // -- attach / shutdown -------------------------------------------------

    void start_io_thread() {
        threadman::ManagedThread::Options topts;
        topts.name = "conduit:amqp-io";
        io_thread = std::make_unique<threadman::ManagedThread>(
            std::move(topts), [this](const std::stop_token& tok) { io_loop(tok); });
    }

    void shutdown() noexcept {
        if (!io_thread) {
            connection.reset();
            channel.reset();
            return;
        }
        // Run close on the IO thread, then stop the loop.
        std::promise<void> closed;
        const auto fut = closed.get_future();
        post([this, &closed]() {
            try {
                if (channel) {
                    channel->close();
                }
            } catch (...) {}
            try {
                if (connection) {
                    connection->close();
                }
            } catch (...) {}
            try {
                closed.set_value();
            } catch (...) {}
        });
        try {
            fut.wait_for(std::chrono::seconds(2));
        } catch (...) {}

        io_thread->request_stop();
        wake();
        try {
            if (io_thread->joinable()) {
                io_thread->join();
            }
        } catch (...) {}
        io_thread.reset();
        channel.reset();
        connection.reset();
    }

    void connect_and_declare() {
        std::promise<std::string> bound_q;  // empty string == ok; "X" == error msg
        auto fut = bound_q.get_future();
        auto shared_p = std::make_shared<std::promise<std::string>>(std::move(bound_q));

        post([this, shared_p]() {
            try {
                AMQP::Address address(config.url);
                connection = std::make_unique<AMQP::TcpConnection>(this, address);
                channel = std::make_unique<AMQP::TcpChannel>(connection.get());

                channel->onError([shared_p, weak_this = this](const char* msg) {
                    (void)weak_this;
                    try {
                        shared_p->set_value(msg != nullptr ? std::string{msg}
                                                           : std::string{"channel error"});
                    } catch (...) {}
                });

                int ex_flags = 0;
                if (config.exchange_durable) {
                    ex_flags |= AMQP::durable;
                }
                if (config.exchange_auto_delete) {
                    ex_flags |= AMQP::autodelete;
                }
                AMQP::ExchangeType ex_type = AMQP::topic;
                if (config.exchange_type == "direct") {
                    ex_type = AMQP::direct;
                } else if (config.exchange_type == "fanout") {
                    ex_type = AMQP::fanout;
                } else if (config.exchange_type == "headers") {
                    ex_type = AMQP::headers;
                }
                channel->declareExchange(config.exchange, ex_type, ex_flags)
                    .onError([shared_p](const char* msg) {
                        try {
                            shared_p->set_value(msg != nullptr ? std::string{msg}
                                                               : std::string{"declareExchange"});
                        } catch (...) {}
                    });

                int q_flags = 0;
                if (config.queue_durable) {
                    q_flags |= AMQP::durable;
                }
                if (config.queue_exclusive) {
                    q_flags |= AMQP::exclusive;
                }
                if (config.queue_auto_delete) {
                    q_flags |= AMQP::autodelete;
                }
                channel->declareQueue(config.queue, q_flags)
                    .onSuccess([this, shared_p](const std::string& name,
                                                std::uint32_t /*msgs*/,
                                                std::uint32_t /*consumers*/) {
                        bound_queue = name;
                        channel->bindQueue(config.exchange, name, config.routing_key)
                            .onSuccess([this, shared_p]() {
                                channel->consume(bound_queue)
                                    .onReceived([this](const AMQP::Message& message,
                                                       const std::uint64_t delivery_tag,
                                                       bool /*redelivered*/) {
                                        if (channel) {
                                            channel->ack(delivery_tag);
                                        }
                                        deliver_message(message);
                                    })
                                    .onSuccess([shared_p]() {
                                        try {
                                            shared_p->set_value("");
                                        } catch (...) {}
                                    })
                                    .onError([shared_p](const char* msg) {
                                        try {
                                            shared_p->set_value(msg != nullptr
                                                                    ? std::string{msg}
                                                                    : std::string{"consume"});
                                        } catch (...) {}
                                    });
                            })
                            .onError([shared_p](const char* msg) {
                                try {
                                    shared_p->set_value(msg != nullptr ? std::string{msg}
                                                                       : std::string{"bindQueue"});
                                } catch (...) {}
                            });
                    })
                    .onError([shared_p](const char* msg) {
                        try {
                            shared_p->set_value(msg != nullptr ? std::string{msg}
                                                               : std::string{"declareQueue"});
                        } catch (...) {}
                    });

                if (confirms_enabled) {
                    channel->confirmSelect()
                        .onAck([this](const std::uint64_t delivery_tag, const bool multiple) {
                            resolve_confirm(delivery_tag, multiple, true);
                        })
                        .onNack([this](const std::uint64_t delivery_tag,
                                       const bool multiple,
                                       bool /*req*/) {
                            resolve_confirm(delivery_tag, multiple, false);
                        });
                }
            } catch (const std::exception& e) {
                try {
                    shared_p->set_value(std::string{"connect: "} + e.what());
                } catch (...) {}
            } catch (...) {
                try {
                    shared_p->set_value("connect: unknown error");
                } catch (...) {}
            }
        });

        if (const auto status = fut.wait_for(config.connect_timeout);
            status != std::future_status::ready) {
            throw AmqpError{"conduit::amqp::Transport::attach: connect/declare timed out"};
        }
        if (const std::string err = fut.get(); !err.empty()) {
            throw AmqpError{std::string{"conduit::amqp::Transport::attach: "} + err};
        }
        if (error_seen.load(std::memory_order_acquire)) {
            throw AmqpError{std::string{"conduit::amqp::Transport::attach: "} + last_error};
        }
    }

    void resolve_confirm(const std::uint64_t delivery_tag, const bool multiple, const bool ok) {
        std::scoped_lock lock(confirms_mu);
        if (multiple) {
            for (auto it = pending_confirms.begin(); it != pending_confirms.end();) {
                if (it->first <= delivery_tag) {
                    try {
                        it->second.set_value(ok);
                    } catch (...) {}
                    it = pending_confirms.erase(it);
                } else {
                    ++it;
                }
            }
        } else {
            if (const auto it = pending_confirms.find(delivery_tag); it != pending_confirms.end()) {
                try {
                    it->second.set_value(ok);
                } catch (...) {}
                pending_confirms.erase(it);
            }
        }
    }

    void deliver_message(const AMQP::Message& message) const {
        if (!sink || !registry) {
            return;
        }
        try {
            const std::string_view payload{message.body(),
                                           static_cast<std::size_t>(message.bodySize())};
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

Transport::~Transport() = default;

void Transport::attach_with_sink(Bus& bus, InboundSink sink) {
    conduit::Transport::attach_with_sink(bus, std::move(sink));
    if (!impl_->registry) {
        impl_->registry = bus.registry();
    }
    impl_->sink = [this](const EventEnvelopeView& v) { this->deliver_inbound(v); };
    impl_->error_sink = [this](const std::exception_ptr& ep) {
        if (const auto* b = this->bus()) {
            b->report_transport_error("amqp", ep);
        }
    };

    impl_->start_io_thread();
    try {
        impl_->connect_and_declare();
    } catch (...) {
        impl_->shutdown();
        throw;
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
    if (!impl_ || !impl_->connection) {
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

    const bool require_ack = v.flags().contains<flags::RequireAck>() && impl_->confirms_enabled;
    std::future<bool> ack_fut;
    std::shared_ptr<std::promise<bool>> ack_promise;
    if (require_ack) {
        ack_promise = std::make_shared<std::promise<bool>>();
        ack_fut = ack_promise->get_future();
    }

    const std::string content_type =
        impl_->config.format == Format::Cbor ? "application/cbor" : "application/json";

    impl_->post([this, p = std::move(payload), content_type, ack_promise = ack_promise]() mutable {
        if (!impl_->channel) {
            if (ack_promise) {
                try {
                    ack_promise->set_value(false);
                } catch (...) {}
            }
            return;
        }
        try {
            AMQP::Envelope env(p.data(), p.size());
            env.setContentType(content_type);
            if (impl_->config.persistent) {
                env.setDeliveryMode(2);
            }
            const std::uint64_t tag = impl_->next_delivery_tag++;
            if (ack_promise) {
                std::scoped_lock lock(impl_->confirms_mu);
                impl_->pending_confirms.emplace(tag, std::move(*ack_promise));
            }
            const bool ok =
                impl_->channel->publish(impl_->config.exchange, impl_->config.routing_key, env);
            if (!ok && ack_promise) {
                std::scoped_lock lock(impl_->confirms_mu);
                if (const auto it = impl_->pending_confirms.find(tag);
                    it != impl_->pending_confirms.end()) {
                    try {
                        it->second.set_value(false);
                    } catch (...) {}
                    impl_->pending_confirms.erase(it);
                }
            }
        } catch (...) {
            if (ack_promise) {
                try {
                    ack_promise->set_value(false);
                } catch (...) {}
            }
        }
    });

    if (require_ack && ack_fut.valid()) {
        (void)ack_fut.wait_for(std::chrono::seconds(5));
    }
}

void Transport::flush() {
    if (!impl_) {
        return;
    }
    // Best-effort: post an empty task and wait for the IO thread to process it,
    // so any queued publishes have at least been handed to AMQP-CPP.
    std::promise<void> done;
    const auto fut = done.get_future();
    impl_->post([&done]() {
        try {
            done.set_value();
        } catch (...) {}
    });
    (void)fut.wait_for(std::chrono::seconds(1));
}

bool Transport::is_connected() const noexcept {
    return impl_ && impl_->connected.load(std::memory_order_acquire);
}

}  // namespace conduit::amqp
