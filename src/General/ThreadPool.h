#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// Persistent worker pool sized to hardware concurrency.
class ThreadPool {
 public:
  static ThreadPool& Instance() {
    static ThreadPool inst(std::max<size_t>(1u, std::thread::hardware_concurrency()));
    return inst;
  }

  [[nodiscard]] size_t Size() const { return workers_.size(); }

  // Deterministic simulation switch. When enabled, ParallelChunks and ECS ParallelForEach run
  // serially on the calling thread so work-stealing order and floating-point accumulation order
  // are identical across runs (required for seeded bot replays / paired A/B experiments).
  static void SetForceSerial(const bool serial) { ForceSerialFlag().store(serial, std::memory_order_relaxed); }
  [[nodiscard]] static bool IsForceSerial() { return ForceSerialFlag().load(std::memory_order_relaxed); }

  // Runs fn(batchIndex, begin, end) over contiguous sub-ranges in parallel across the pool.
  // fn must be thread-safe for disjoint writes and must not be called from a worker thread.
  template <typename Fn>
  static void ParallelChunks(size_t count, Fn&& fn) {
    if (count == 0) return;

    const size_t numBatches = IsForceSerial() ? 1 : std::min(count, Instance().Size());
    if (numBatches <= 1) {
      fn(static_cast<size_t>(0), static_cast<size_t>(0), count);
      return;
    }

    // Synchronizes batch completion before destroying barrier.
    struct Barrier {
      size_t remaining;
      std::mutex mutex;
      std::condition_variable cv;
      explicit Barrier(size_t n) : remaining(n) {}
      void Signal() {
        std::lock_guard<std::mutex> lk(mutex);
        if (--remaining == 0) cv.notify_one();
      }
      void Wait() {
        std::unique_lock<std::mutex> lk(mutex);
        cv.wait(lk, [this] { return remaining == 0; });
      }
    } barrier(numBatches - 1);

    const size_t per = (count + numBatches - 1) / numBatches;
    struct Context {
      std::remove_reference_t<Fn>& fn;
      Barrier& barrier;
      size_t count{0};
      size_t per{0};
    } ctx{fn, barrier, count, per};

    for (size_t b = 0; b < numBatches - 1; ++b) {
      Instance().Submit([&ctx, b] {
        const size_t begin = b * ctx.per;
        const size_t end = std::min(begin + ctx.per, ctx.count);
        ctx.fn(b, begin, end);
        ctx.barrier.Signal();
      });
    }

    // Caller runs the final batch inline rather than sitting idle, then waits for the dispatched
    // ones. The barrier count equals the number of Submits regardless of whether any range is
    // empty, so there is no deadlock even on a ragged final split.
    const size_t inlineBegin = (numBatches - 1) * per;
    if (inlineBegin < count) {
      fn(numBatches - 1, inlineBegin, count);
    }
    barrier.Wait();
  }

  void Submit(std::function<void()> task) {
    {
      std::lock_guard<std::mutex> lk(jobs_mutex_);
      if (count_ == jobs_.size()) {
        const size_t oldSize = jobs_.size();
        const size_t newSize = oldSize * 2;
        std::vector<std::function<void()>> newJobs(newSize);
        for (size_t i = 0; i < count_; ++i) {
          newJobs[i] = std::move(jobs_[(head_ + i) % oldSize]);
        }
        jobs_ = std::move(newJobs);
        head_ = 0;
      }
      const size_t tail = (head_ + count_) % jobs_.size();
      jobs_[tail] = std::move(task);
      ++count_;
    }
    jobs_cv_.notify_one();
  }

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;

  ~ThreadPool() {
    {
      std::lock_guard<std::mutex> lk(jobs_mutex_);
      stop_ = true;
    }
    jobs_cv_.notify_all();
    for (auto& t : workers_) {
      if (t.joinable()) t.join();
    }
  }

 private:
  static std::atomic<bool>& ForceSerialFlag() {
    static std::atomic<bool> flag{false};
    return flag;
  }

  explicit ThreadPool(size_t numThreads) {
    const size_t initialCapacity = std::max<size_t>(128u, numThreads * 8);
    jobs_.resize(initialCapacity);
    workers_.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
      workers_.emplace_back([this] { WorkerLoop(); });
    }
  }

  void WorkerLoop() {
    for (;;) {
      std::function<void()> job;
      {
        std::unique_lock<std::mutex> lk(jobs_mutex_);
        jobs_cv_.wait(lk, [this] { return stop_ || count_ > 0; });
        if (stop_ && count_ == 0) return;
        job = std::move(jobs_[head_]);
        jobs_[head_] = nullptr;
        head_ = (head_ + 1) % jobs_.size();
        --count_;
      }
      job();
    }
  }

  std::vector<std::thread> workers_;
  std::vector<std::function<void()>> jobs_;
  size_t head_ = 0;
  size_t count_ = 0;
  std::mutex jobs_mutex_;
  std::condition_variable jobs_cv_;
  bool stop_ = false;
};
