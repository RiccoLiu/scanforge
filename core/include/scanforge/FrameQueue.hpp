#pragma once

// Bounded blocking queue for the acquisition -> processing hand-off.
//
// IMPORTANT: this is the MUTEX version on purpose. Do not start with a lock-free
// ring buffer. Being able to explain back-pressure, close/drain and spurious
// wakeups is worth far more than shaving a microsecond. A lock-free version is
// a stage-4 optimisation, after a benchmark proves the mutex is the bottleneck.
// Reference: Qt's QThread + queued-connection producer/consumer examples,
// PDAL's stage buffering (BSD), folly/MPMCQueue (Apache-2.0) for later study.

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace sf {

template <class T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity)
        : capacity_(capacity == 0 ? 1 : capacity) {}

    enum class PushOutcome { Accepted, Closed };

    // Blocks while full. Returns Closed when close() was called.
    PushOutcome push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        notFull_.wait(lock, [this] {
            return closed_ || queue_.size() < capacity_;
        });
        if (closed_) return PushOutcome::Closed;
        queue_.push_back(std::move(item));
        lock.unlock();
        notEmpty_.notify_one();
        return PushOutcome::Accepted;
    }

    // Blocks while empty. Returns nullopt only after close() AND full drain,
    // which is exactly the "finish the frames already in flight" semantics.
    [[nodiscard]] std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        notEmpty_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) return std::nullopt;   // implies closed_
        T item = std::move(queue_.front());
        queue_.pop_front();
        lock.unlock();
        notFull_.notify_one();
        return item;
    }

    void close() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        notFull_.notify_all();
        notEmpty_.notify_all();
    }

    [[nodiscard]] bool isClosed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    const std::size_t       capacity_;
    mutable std::mutex      mutex_;
    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
    std::deque<T>           queue_;
    bool                    closed_ = false;
};

} // namespace sf
