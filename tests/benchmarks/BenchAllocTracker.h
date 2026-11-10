#pragma once

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>

struct lua_State;

namespace bench {

struct AllocStats {
  int64_t allocCount = 0;
  int64_t deallocCount = 0;
  int64_t allocatedBytes = 0;
  int64_t deallocatedBytes = 0;
  int64_t currentLiveBytes = 0;
  int64_t peakLiveBytes = 0;
};

[[nodiscard]] AllocStats GetCurrentAllocStats() noexcept;
void ResetAllocStats() noexcept;
void SetAllocTrackingEnabled(bool enabled) noexcept;
[[nodiscard]] bool IsAllocTrackingEnabled() noexcept;

// RAII scope to track C++ operator new/delete allocations across benchmark iterations.
class AllocScope {
 public:
  AllocScope();
  ~AllocScope();
  AllocScope(const AllocScope&) = delete;
  AllocScope& operator=(const AllocScope&) = delete;

  void Stop();
  void Report(benchmark::State& state, double iterations = 0.0) const;

 private:
  AllocStats startStats_{};
  AllocStats endStats_{};
  bool stopped_ = false;
};

// RAII scope to track Lua 5.4 VM allocations and GC heap growth.
class LuaMemoryScope {
 public:
  explicit LuaMemoryScope(lua_State* L);
  ~LuaMemoryScope();
  LuaMemoryScope(const LuaMemoryScope&) = delete;
  LuaMemoryScope& operator=(const LuaMemoryScope&) = delete;

  void Stop();
  void Report(benchmark::State& state, double iterations = 0.0) const;

 private:
  lua_State* L_ = nullptr;
  void* oldUd_ = nullptr;
  void* (*oldAlloc_)(void*, void*, size_t, size_t) = nullptr;
  int64_t allocCount_ = 0;
  int64_t allocBytes_ = 0;
  int64_t freeBytes_ = 0;
  int64_t startKb_ = 0;
  int64_t endKb_ = 0;
  int64_t peakBytes_ = 0;
  int64_t currentLiveBytes_ = 0;
  bool stopped_ = false;

  static void* TrackingLuaAlloc(void* ud, void* ptr, size_t osize, size_t nsize);
};

}  // namespace bench
