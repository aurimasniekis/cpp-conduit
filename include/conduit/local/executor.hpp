#pragma once

/// @file
/// @brief Internal executor primitives used by `local::Transport` for the
///        Queue and ThreadPool execution modes.

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace conduit::local::detail {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t threads, const std::size_t queue_capacity = 0)
        : queue_capacity_(queue_capacity) {
        if (threads == 0) {
            threads = 1;
        }
        threads_.reserve(threads);
        for (std::size_t i = 0; i < threads; ++i) {
            threads_.emplace_back([this] { worker_loop(); });
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    ~ThreadPool() {
        shutdown();
    }

    /// Submit a unit of work. Blocks when the queue is full (capacity > 0).
    void submit(std::function<void()> fn) {
        std::unique_lock lock(mu_);
        not_full_.wait(lock, [this] {
            return stopping_ || queue_capacity_ == 0 || queue_.size() < queue_capacity_;
        });
        if (stopping_) {
            return;
        }
        queue_.push_back(std::move(fn));
        ++pending_;
        not_empty_.notify_one();
    }

    /// Block until all submitted work has completed.
    void wait_idle() {
        std::unique_lock lock(mu_);
        idle_.wait(lock, [this] { return pending_ == 0; });
    }

    void shutdown() noexcept {
        {
            std::scoped_lock lock(mu_);
            if (stopping_) {
                return;
            }
            stopping_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
        for (auto& t : threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        threads_.clear();
    }

private:
    void worker_loop() {
        for (;;) {
            std::function<void()> fn;
            {
                std::unique_lock lock(mu_);
                not_empty_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (stopping_ && queue_.empty()) {
                    return;
                }
                fn = std::move(queue_.front());
                queue_.pop_front();
                not_full_.notify_one();
            }
            try {
                fn();
            } catch (...) {
                // Transports catch listener exceptions; pool worker just guards.
            }
            {
                std::scoped_lock lock(mu_);
                if (--pending_ == 0) {
                    idle_.notify_all();
                }
            }
        }
    }

    std::mutex mu_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::condition_variable idle_;
    std::deque<std::function<void()>> queue_;
    std::vector<std::thread> threads_;
    std::size_t queue_capacity_;
    std::size_t pending_ = 0;
    bool stopping_ = false;
};

}  // namespace conduit::local::detail
