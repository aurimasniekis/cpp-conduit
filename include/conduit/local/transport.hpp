#pragma once

/// @file
/// @brief In-process transport with Direct / Queue / ThreadPool execution modes.

#include <conduit/bus.hpp>
#include <conduit/envelope.hpp>
#include <conduit/flags.hpp>
#include <conduit/transport.hpp>

#include <threadman/manager.hpp>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace conduit::local {

enum class Execution : std::uint8_t { Direct, Queue, ThreadPool };

/// Configuration for the Queue / ThreadPool execution modes. This is threadman's
/// own `ThreadPoolOptions`, so the full pool surface is available — dynamic
/// scaling (`min_workers` / `max_workers`), idle retirement, scale thresholds,
/// a custom `name`, … Defaults match threadman: `max_workers` defaults to
/// `std::thread::hardware_concurrency()` and `min_workers` to 1, i.e. the pool
/// scales on demand. Set `min_workers == max_workers` for a fixed-size pool.
///
/// `max_queue_size` is honoured as a **blocking** back-pressure bound: when set,
/// `dispatch()` blocks the producer until a slot frees, rather than threadman's
/// native reject-on-overflow (which would drop envelopes). 0 = unbounded.
using ThreadPoolConfig = threadman::ThreadPoolOptions;

class Transport : public conduit::Transport {
public:
    explicit Transport(const Execution mode = Execution::Direct, ThreadPoolConfig cfg = {})
        : mode_(mode) {
        if (mode_ == Execution::Direct) {
            return;
        }
        // Give the pool a conduit-branded name (so threadman's tm_pool_* series
        // are attributable) unless the caller set one of their own. threadman's
        // default name isn't empty, so treat it as "unset" too.
        const bool name_is_default =
            cfg.name.empty() || cfg.name == threadman::ThreadPoolOptions{}.name;
        if (mode_ == Execution::Queue) {
            // A queue is a single serialized worker, whatever counts were given.
            cfg.min_workers = 1;
            cfg.max_workers = 1;
            if (name_is_default) {
                cfg.name = "conduit:local-queue";
            }
        } else if (name_is_default) {  // ThreadPool
            cfg.name = "conduit:local";
        }
        // Capture the requested bound for our own blocking back-pressure, then
        // hand threadman an unbounded queue so it never *rejects* (drops) an
        // envelope on overflow — dispatch() blocks the producer instead.
        capacity_ = cfg.max_queue_size;
        cfg.max_queue_size = 0;
        pool_ = std::make_unique<threadman::ThreadPool>(std::move(cfg));
    }

    static Transport thread_pool(ThreadPoolConfig cfg = {}) {
        return Transport{Execution::ThreadPool, std::move(cfg)};
    }
    static Transport queued(const std::size_t queue_capacity = 0) {
        ThreadPoolConfig cfg;
        cfg.max_queue_size = queue_capacity;
        return Transport{Execution::Queue, std::move(cfg)};
    }

    [[nodiscard]] TransportScope scope() const noexcept override {
        return TransportScope::Local;
    }

    void detach() noexcept override {
        flush();
    }

    void dispatch(const EventEnvelopeView& v) override {
        if (const bool force_direct = v.flags().contains<flags::Direct>();
            mode_ == Execution::Direct || force_direct || !pool_) {
            deliver_inbound(v);
            return;
        }
        EventEnvelopeView snap = v;  // shared core/payload
        {
            std::unique_lock lock(drain_mu_);
            // Bounded-queue back-pressure: block the producer while there are
            // already `capacity_` units in flight. threadman's own
            // `max_queue_size` *rejects* on overflow (throws) rather than
            // blocking, which would drop events — so the pool queue stays
            // unbounded and this semaphore preserves the blocking semantics
            // without ever losing an envelope.
            if (capacity_ != 0) {
                space_cv_.wait(lock, [this] { return outstanding_ < capacity_; });
            }
            ++outstanding_;
        }
        try {
            pool_->execute([this, snap]() mutable {
                try {
                    deliver_inbound(snap);
                } catch (...) {
                    // deliver_inbound routes listener exceptions through the bus
                    // middleware; guard here so nothing escapes the pool worker.
                }
                complete_one();
            });
        } catch (...) {
            // Submission failed (e.g. pool shutting down) — undo the reservation
            // so flush() can still reach zero.
            complete_one();
        }
    }

    void flush() override {
        if (!pool_) {
            return;
        }
        std::unique_lock lock(drain_mu_);
        drained_cv_.wait(lock, [this] { return outstanding_ == 0; });
    }

    [[nodiscard]] Execution mode() const noexcept {
        return mode_;
    }

private:
    void complete_one() {
        std::scoped_lock lock(drain_mu_);
        if (--outstanding_ == 0) {
            drained_cv_.notify_all();
        }
        space_cv_.notify_one();
    }

    Execution mode_;
    std::unique_ptr<threadman::ThreadPool> pool_;
    std::size_t capacity_ = 0;  // blocking back-pressure bound (0 = unbounded)

    std::mutex drain_mu_;
    std::condition_variable drained_cv_;  // fires when outstanding_ hits 0
    std::condition_variable space_cv_;    // fires when a bounded-queue slot frees
    std::size_t outstanding_ = 0;
};

}  // namespace conduit::local
