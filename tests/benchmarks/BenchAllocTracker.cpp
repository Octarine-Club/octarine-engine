#include "BenchAllocTracker.h"

#include <atomic>
#include <cstdlib>
#include <lua.hpp>
#include <new>

#if defined(__APPLE__)
#include <malloc/malloc.h>
inline size_t PlatformGetSize(void* ptr) { return ptr ? malloc_size(ptr) : 0; }
inline size_t PlatformGetAlignedSize(void* ptr, size_t) { return ptr ? malloc_size(ptr) : 0; }
#elif defined(_WIN32)
#include <malloc.h>
inline size_t PlatformGetSize(void* ptr) { return ptr ? _msize(ptr) : 0; }
inline size_t PlatformGetAlignedSize(void* ptr, size_t al) { return ptr ? _aligned_msize(ptr, al, 0) : 0; }
#elif defined(__linux__)
#include <malloc.h>
inline size_t PlatformGetSize(void* ptr) { return ptr ? malloc_usable_size(ptr) : 0; }
inline size_t PlatformGetAlignedSize(void* ptr, size_t) { return ptr ? malloc_usable_size(ptr) : 0; }
#else
inline size_t PlatformGetSize(void*) { return 0; }
inline size_t PlatformGetAlignedSize(void*, size_t) { return 0; }
#endif

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define OCTARINE_HAS_SANITIZER 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
#define OCTARINE_HAS_SANITIZER 1
#endif
#endif

#if !defined(OCTARINE_HAS_SANITIZER)
static std::atomic<bool> g_trackAllocations{false};
static std::atomic<int64_t> g_allocCount{0};
static std::atomic<int64_t> g_deallocCount{0};
static std::atomic<int64_t> g_allocatedBytes{0};
static std::atomic<int64_t> g_deallocatedBytes{0};
static std::atomic<int64_t> g_currentLiveBytes{0};
static std::atomic<int64_t> g_peakLiveBytes{0};

inline void RecordAlloc(void* ptr, size_t size) {
  if (g_trackAllocations.load(std::memory_order_relaxed)) {
    const size_t actualSize = PlatformGetSize(ptr);
    const size_t reportedSize = actualSize > 0 ? actualSize : size;
    g_allocCount.fetch_add(1, std::memory_order_relaxed);
    g_allocatedBytes.fetch_add(static_cast<int64_t>(reportedSize), std::memory_order_relaxed);
    const int64_t current =
        g_currentLiveBytes.fetch_add(static_cast<int64_t>(reportedSize), std::memory_order_relaxed) +
        static_cast<int64_t>(reportedSize);
    int64_t peak = g_peakLiveBytes.load(std::memory_order_relaxed);
    while (current > peak && !g_peakLiveBytes.compare_exchange_weak(peak, current, std::memory_order_relaxed));
  }
}

inline void RecordAllocAligned(void* ptr, size_t size, size_t al) {
  if (g_trackAllocations.load(std::memory_order_relaxed)) {
    const size_t actualSize = PlatformGetAlignedSize(ptr, al);
    const size_t reportedSize = actualSize > 0 ? actualSize : size;
    g_allocCount.fetch_add(1, std::memory_order_relaxed);
    g_allocatedBytes.fetch_add(static_cast<int64_t>(reportedSize), std::memory_order_relaxed);
    const int64_t current =
        g_currentLiveBytes.fetch_add(static_cast<int64_t>(reportedSize), std::memory_order_relaxed) +
        static_cast<int64_t>(reportedSize);
    int64_t peak = g_peakLiveBytes.load(std::memory_order_relaxed);
    while (current > peak && !g_peakLiveBytes.compare_exchange_weak(peak, current, std::memory_order_relaxed));
  }
}

inline void RecordDealloc(void* ptr, size_t size) {
  if (g_trackAllocations.load(std::memory_order_relaxed)) {
    const size_t actualSize = PlatformGetSize(ptr);
    const size_t reportedSize = actualSize > 0 ? actualSize : size;
    g_deallocCount.fetch_add(1, std::memory_order_relaxed);
    g_deallocatedBytes.fetch_add(static_cast<int64_t>(reportedSize), std::memory_order_relaxed);
    g_currentLiveBytes.fetch_sub(static_cast<int64_t>(reportedSize), std::memory_order_relaxed);
  }
}

inline void RecordDeallocAligned(void* ptr, size_t size, size_t al) {
  if (g_trackAllocations.load(std::memory_order_relaxed)) {
    const size_t actualSize = PlatformGetAlignedSize(ptr, al);
    const size_t reportedSize = actualSize > 0 ? actualSize : size;
    g_deallocCount.fetch_add(1, std::memory_order_relaxed);
    g_deallocatedBytes.fetch_add(static_cast<int64_t>(reportedSize), std::memory_order_relaxed);
    g_currentLiveBytes.fetch_sub(static_cast<int64_t>(reportedSize), std::memory_order_relaxed);
  }
}

void* operator new(std::size_t size) {
  void* ptr = std::malloc(size);
  if (!ptr) throw std::bad_alloc();
  RecordAlloc(ptr, size);
  return ptr;
}

void* operator new[](std::size_t size) {
  void* ptr = std::malloc(size);
  if (!ptr) throw std::bad_alloc();
  RecordAlloc(ptr, size);
  return ptr;
}

void* operator new(std::size_t size, std::align_val_t al) {
  void* ptr = nullptr;
#if defined(_WIN32)
  ptr = _aligned_malloc(size, static_cast<size_t>(al));
#else
  if (posix_memalign(&ptr, static_cast<size_t>(al), size) != 0) {
    ptr = nullptr;
  }
#endif
  if (!ptr) throw std::bad_alloc();
  RecordAllocAligned(ptr, size, static_cast<size_t>(al));
  return ptr;
}

void* operator new[](std::size_t size, std::align_val_t al) { return ::operator new(size, al); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  void* ptr = std::malloc(size);
  if (ptr) RecordAlloc(ptr, size);
  return ptr;
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  void* ptr = std::malloc(size);
  if (ptr) RecordAlloc(ptr, size);
  return ptr;
}

void operator delete(void* ptr) noexcept {
  if (!ptr) return;
  RecordDealloc(ptr, 0);
  std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
  if (!ptr) return;
  RecordDealloc(ptr, 0);
  std::free(ptr);
}

void operator delete(void* ptr, std::size_t size) noexcept {
  if (!ptr) return;
  RecordDealloc(ptr, size);
  std::free(ptr);
}

void operator delete[](void* ptr, std::size_t size) noexcept {
  if (!ptr) return;
  RecordDealloc(ptr, size);
  std::free(ptr);
}

void operator delete(void* ptr, std::align_val_t al) noexcept {
  if (!ptr) return;
  RecordDeallocAligned(ptr, 0, static_cast<size_t>(al));
#if defined(_WIN32)
  _aligned_free(ptr);
#else
  std::free(ptr);
#endif
}

void operator delete[](void* ptr, std::align_val_t al) noexcept { ::operator delete(ptr, al); }

void operator delete(void* ptr, std::size_t size, std::align_val_t al) noexcept {
  if (!ptr) return;
  RecordDeallocAligned(ptr, size, static_cast<size_t>(al));
#if defined(_WIN32)
  _aligned_free(ptr);
#else
  std::free(ptr);
#endif
}

void operator delete[](void* ptr, std::size_t size, std::align_val_t al) noexcept { ::operator delete(ptr, size, al); }
#endif

namespace bench {

AllocStats GetCurrentAllocStats() noexcept {
#if !defined(OCTARINE_HAS_SANITIZER)
  AllocStats stats;
  stats.allocCount = g_allocCount.load(std::memory_order_relaxed);
  stats.deallocCount = g_deallocCount.load(std::memory_order_relaxed);
  stats.allocatedBytes = g_allocatedBytes.load(std::memory_order_relaxed);
  stats.deallocatedBytes = g_deallocatedBytes.load(std::memory_order_relaxed);
  stats.currentLiveBytes = g_currentLiveBytes.load(std::memory_order_relaxed);
  stats.peakLiveBytes = g_peakLiveBytes.load(std::memory_order_relaxed);
  return stats;
#else
  return AllocStats{};
#endif
}

void ResetAllocStats() noexcept {
#if !defined(OCTARINE_HAS_SANITIZER)
  g_allocCount.store(0, std::memory_order_relaxed);
  g_deallocCount.store(0, std::memory_order_relaxed);
  g_allocatedBytes.store(0, std::memory_order_relaxed);
  g_deallocatedBytes.store(0, std::memory_order_relaxed);
  g_currentLiveBytes.store(0, std::memory_order_relaxed);
  g_peakLiveBytes.store(0, std::memory_order_relaxed);
#endif
}

void SetAllocTrackingEnabled(bool enabled) noexcept {
#if !defined(OCTARINE_HAS_SANITIZER)
  g_trackAllocations.store(enabled, std::memory_order_relaxed);
#else
  (void)enabled;
#endif
}

bool IsAllocTrackingEnabled() noexcept {
#if !defined(OCTARINE_HAS_SANITIZER)
  return g_trackAllocations.load(std::memory_order_relaxed);
#else
  return false;
#endif
}

AllocScope::AllocScope() {
#if !defined(OCTARINE_HAS_SANITIZER)
  g_peakLiveBytes.store(g_currentLiveBytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
#endif
  SetAllocTrackingEnabled(true);
  startStats_ = GetCurrentAllocStats();
}

AllocScope::~AllocScope() {
  if (!stopped_) {
    Stop();
  }
}

void AllocScope::Stop() {
  if (stopped_) return;
  endStats_ = GetCurrentAllocStats();
  SetAllocTrackingEnabled(false);
  stopped_ = true;
}

void AllocScope::Report(benchmark::State& state, const double iterations) const {
  const double iters = iterations > 0.0 ? iterations : static_cast<double>(state.iterations());
  const int64_t allocs = endStats_.allocCount - startStats_.allocCount;
  const int64_t bytes = endStats_.allocatedBytes - startStats_.allocatedBytes;
  const int64_t netGrowth = (endStats_.allocatedBytes - endStats_.deallocatedBytes) -
                            (startStats_.allocatedBytes - startStats_.deallocatedBytes);
  if (iters > 0.0) {
    state.counters["allocs"] = static_cast<double>(allocs) / iters;
    state.counters["alloc_bytes"] = static_cast<double>(bytes) / iters;
  }
  state.counters["net_heap_bytes"] = static_cast<double>(netGrowth);
  const int64_t peakLiveGrowth = std::max<int64_t>(0, endStats_.peakLiveBytes - startStats_.currentLiveBytes);
  state.counters["peak_live_kb"] = static_cast<double>(peakLiveGrowth) / 1024.0;
}

void* LuaMemoryScope::TrackingLuaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  auto* self = static_cast<LuaMemoryScope*>(ud);
  if (nsize == 0) {
    if (ptr != nullptr) {
      self->freeBytes_ += static_cast<int64_t>(osize);
      self->currentLiveBytes_ -= static_cast<int64_t>(osize);
    }
    return self->oldAlloc_(self->oldUd_, ptr, osize, nsize);
  }
  if (ptr == nullptr) {
    self->allocCount_ += 1;
    self->allocBytes_ += static_cast<int64_t>(nsize);
    self->currentLiveBytes_ += static_cast<int64_t>(nsize);
    if (self->currentLiveBytes_ > self->peakBytes_) {
      self->peakBytes_ = self->currentLiveBytes_;
    }
  } else {
    if (nsize > osize) {
      self->allocCount_ += 1;
      const size_t diff = nsize - osize;
      self->allocBytes_ += static_cast<int64_t>(diff);
      self->currentLiveBytes_ += static_cast<int64_t>(diff);
      if (self->currentLiveBytes_ > self->peakBytes_) {
        self->peakBytes_ = self->currentLiveBytes_;
      }
    } else {
      self->freeBytes_ += static_cast<int64_t>(osize - nsize);
      self->currentLiveBytes_ -= static_cast<int64_t>(osize - nsize);
    }
  }
  return self->oldAlloc_(self->oldUd_, ptr, osize, nsize);
}

LuaMemoryScope::LuaMemoryScope(lua_State* L) : L_(L) {
  if (L_ != nullptr) {
    startKb_ = static_cast<int64_t>(lua_gc(L_, LUA_GCCOUNT, 0));
    oldAlloc_ = lua_getallocf(L_, &oldUd_);
    lua_setallocf(L_, &TrackingLuaAlloc, this);
  }
}

LuaMemoryScope::~LuaMemoryScope() {
  if (!stopped_) {
    Stop();
  }
}

void LuaMemoryScope::Stop() {
  if (stopped_) return;
  if (L_ != nullptr) {
    endKb_ = static_cast<int64_t>(lua_gc(L_, LUA_GCCOUNT, 0));
    lua_setallocf(L_, oldAlloc_, oldUd_);
  }
  stopped_ = true;
}

void LuaMemoryScope::Report(benchmark::State& state, const double iterations) const {
  const double iters = iterations > 0.0 ? iterations : static_cast<double>(state.iterations());
  if (iters > 0.0) {
    state.counters["lua_allocs"] = static_cast<double>(allocCount_) / iters;
    state.counters["lua_alloc_bytes"] = static_cast<double>(allocBytes_) / iters;
    state.counters["lua_freed_bytes"] = static_cast<double>(freeBytes_) / iters;
  }
  state.counters["lua_peak_kb"] = static_cast<double>(peakBytes_) / 1024.0;
  state.counters["lua_gc_kb"] = static_cast<double>(endKb_);
  state.counters["lua_gc_growth_kb"] = static_cast<double>(endKb_ - startKb_);
}

}  // namespace bench
