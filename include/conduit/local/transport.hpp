#pragma once

/// @file
/// @brief In-process transport with Direct / Queue / ThreadPool execution modes.

#include <conduit/bus.hpp>
#include <conduit/envelope.hpp>
#include <conduit/flags.hpp>
#include <conduit/local/executor.hpp>
#include <conduit/transport.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>

namespace conduit::local {

enum class Execution : std::uint8_t { Direct, Queue, ThreadPool };

struct ThreadPoolConfig {
    std::size_t threads = std::thread::hardware_concurrency();
    std::size_t queue_capacity = 0;  // 0 = unbounded
};

class Transport : public conduit::Transport {
public:
    explicit Transport(const Execution mode = Execution::Direct, ThreadPoolConfig cfg = {})
        : mode_(mode), cfg_(cfg) {
        if (mode_ == Execution::Queue) {
            pool_ = std::make_unique<detail::ThreadPool>(1, cfg.queue_capacity);
        } else if (mode_ == Execution::ThreadPool) {
            pool_ = std::make_unique<detail::ThreadPool>(cfg.threads != 0U ? cfg.threads : 1U,
                                                         cfg.queue_capacity);
        }
    }

    static Transport thread_pool(const ThreadPoolConfig cfg = {}) {
        return Transport{Execution::ThreadPool, cfg};
    }
    static Transport queued(const std::size_t queue_capacity = 0) {
        ThreadPoolConfig cfg;
        cfg.queue_capacity = queue_capacity;
        return Transport{Execution::Queue, cfg};
    }

    [[nodiscard]] TransportScope scope() const noexcept override {
        return TransportScope::Local;
    }

    void detach() noexcept override {
        flush();
    }

    void dispatch(const EventEnvelopeView& v) override {
        if (const bool force_direct = v.flags().has<flags::Direct>();
            mode_ == Execution::Direct || force_direct || !pool_) {
            deliver_inbound(v);
            return;
        }
        EventEnvelopeView snap = v;  // shared core/payload
        pool_->submit([this, snap]() mutable { deliver_inbound(snap); });
    }

    void flush() override {
        if (pool_) {
            pool_->wait_idle();
        }
    }

    [[nodiscard]] Execution mode() const noexcept {
        return mode_;
    }

private:
    Execution mode_;
    ThreadPoolConfig cfg_;
    std::unique_ptr<detail::ThreadPool> pool_;
};

}  // namespace conduit::local
