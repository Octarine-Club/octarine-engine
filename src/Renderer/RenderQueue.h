#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

#include "General/Constants.h"
#include "General/Logger.h"
#include "Renderer/RenderCommands.h"
#include "Renderer/RenderKey.h"

// Lock-free, multi-producer render queue. Pre-sized to a fixed capacity; parallel
// systems atomically claim a slot via fetch_add and write directly. No per-system
// channel + playback merge - that intermediate copy was the bulk of the overhead in
// the previous CommandBuffer-driven design.
//
// Sort() uses an LSB radix sort on the precomputed RenderKey::sortKey. With ~32K+ keys
// this is dramatically faster than std::sort on the full 80-byte RenderKey because the
// radix passes only touch 16-byte (key, srcIdx) entries. The full 80-byte permutation is
// applied via a gather into a scratch buffer at the end - sequential writes are
// prefetch-friendly and the source data fits in L3 cache at typical entry counts.
//
// Capacity must comfortably exceed the max keys-per-frame for the scene; an overflow
// logs a once-per-frame warning and drops excess commands into a thread_local discard slot.
class RenderQueue {
 public:
  using value_type = RenderKey;
  using const_iterator = std::vector<RenderKey>::const_iterator;

  struct LayerYSortConfig {
    bool enabled = true;
    float bandPx = RenderKey::kDefaultYBandPx;
  };

  static constexpr size_t kMaxClipRects = 255;
  static constexpr size_t kMaxConfiguredLayers = 2048;

  explicit RenderQueue(const size_t capacity = Constants::kInitialRenderQueueCapacity)
      : layer_y_sort_(kMaxConfiguredLayers) {
    render_keys_.resize(capacity);
  }

  ~RenderQueue() = default;
  RenderQueue(const RenderQueue&) = delete;
  RenderQueue& operator=(const RenderQueue&) = delete;

  // Atomic and mutex members aren't move-constructible, while Registry::Set takes singletons
  // by value at registration time before concurrent producers attach.
  RenderQueue(RenderQueue&& other) noexcept
      : render_keys_(std::move(other.render_keys_)),
        count_(other.count_.load(std::memory_order_relaxed)),
        overflow_warned_(other.overflow_warned_.load(std::memory_order_relaxed)),
        sort_entries_(std::move(other.sort_entries_)),
        radix_scratch_(std::move(other.radix_scratch_)),
        permute_scratch_(std::move(other.permute_scratch_)),
        clip_rects_(other.clip_rects_),
        clip_count_(other.clip_count_.load(std::memory_order_relaxed)),
        gradients_(std::move(other.gradients_)),
        layer_y_sort_(std::move(other.layer_y_sort_)),
        clear_generation_(other.clear_generation_) {}

  RenderQueue& operator=(RenderQueue&& other) noexcept {
    if (this != &other) {
      render_keys_ = std::move(other.render_keys_);
      count_.store(other.count_.load(std::memory_order_relaxed), std::memory_order_relaxed);
      overflow_warned_.store(other.overflow_warned_.load(std::memory_order_relaxed), std::memory_order_relaxed);
      sort_entries_ = std::move(other.sort_entries_);
      radix_scratch_ = std::move(other.radix_scratch_);
      permute_scratch_ = std::move(other.permute_scratch_);
      clip_rects_ = other.clip_rects_;
      clip_count_.store(other.clip_count_.load(std::memory_order_relaxed), std::memory_order_relaxed);
      gradients_ = std::move(other.gradients_);
      layer_y_sort_ = std::move(other.layer_y_sort_);
      clear_generation_ = other.clear_generation_;
    }
    return *this;
  }

  // Configures per-layer Y-sorting. Flat layers (e.g. floor decals, projectiles, VFX) can pass
  // enabled=false to coalesce multi-texture draws across the whole layer into one batch per atlas.
  void SetLayerYSort(const unsigned int layer, const bool enabled,
                     const float bandPx = RenderKey::kDefaultYBandPx) noexcept {
    if (layer >= layer_y_sort_.size()) return;
    layer_y_sort_[layer] = LayerYSortConfig{enabled, std::max(0.0f, bandPx)};
  }

  void set_layer_y_sort(const unsigned int layer, const bool enabled,
                        const float bandPx = RenderKey::kDefaultYBandPx) noexcept {
    SetLayerYSort(layer, enabled, bandPx);
  }

  [[nodiscard]] LayerYSortConfig GetLayerYSort(const unsigned int layer) const noexcept {
    if (layer < layer_y_sort_.size()) {
      return layer_y_sort_[layer];
    }
    return LayerYSortConfig{};
  }

  void ResetLayerYSort() noexcept { std::fill(layer_y_sort_.begin(), layer_y_sort_.end(), LayerYSortConfig{}); }

  SpriteCommand& EmplaceSprite(unsigned int layer, float depth, const void* batchKey = nullptr,
                               octarine::BlendMode blendMode = octarine::BlendMode::Blend, std::uint8_t clipId = 0,
                               std::uint8_t materialId = 0) {
    auto& key = ClaimSlot();
    InitKeyHeader(key, layer, depth, SPRITE, batchKey, static_cast<std::uint8_t>(blendMode), clipId, materialId);
    return key.payload.sprite;
  }

  SpriteCommand& EmplaceSpriteGradient(unsigned int layer, float depth, const QuadGradient& gradient,
                                       const void* batchKey = nullptr,
                                       octarine::BlendMode blendMode = octarine::BlendMode::Blend,
                                       std::uint8_t clipId = 0, std::uint8_t materialId = 0) {
    const std::uint16_t gradId = RegisterGradient(gradient);
    auto& key = ClaimSlot();
    InitKeyHeader(key, layer, depth, SPRITE, batchKey, static_cast<std::uint8_t>(blendMode), clipId, materialId,
                  gradId);
    return key.payload.sprite;
  }

  SquareCommand& EmplaceSquare(unsigned int layer, float depth, const void* batchKey = nullptr,
                               octarine::BlendMode blendMode = octarine::BlendMode::Blend, std::uint8_t clipId = 0,
                               std::uint8_t materialId = 0) {
    auto& key = ClaimSlot();
    InitKeyHeader(key, layer, depth, SQUARE_PRIMITIVE, batchKey, static_cast<std::uint8_t>(blendMode), clipId,
                  materialId);
    key.payload.square.hasCornerColors = false;
    return key.payload.square;
  }

  TextCommand& EmplaceText(unsigned int layer, float depth, const void* batchKey = nullptr, std::uint8_t clipId = 0,
                           std::uint8_t materialId = 0) {
    auto& key = ClaimSlot();
    InitKeyHeader(key, layer, depth, TEXT, batchKey, static_cast<std::uint8_t>(octarine::BlendMode::Blend), clipId,
                  materialId);
    return key.payload.text;
  }

  CircleCommand& EmplaceCircle(unsigned int layer, float depth, const void* batchKey = nullptr,
                               octarine::BlendMode blendMode = octarine::BlendMode::Blend, std::uint8_t clipId = 0,
                               std::uint8_t materialId = 0) {
    auto& key = ClaimSlot();
    InitKeyHeader(key, layer, depth, CIRCLE_PRIMITIVE, batchKey, static_cast<std::uint8_t>(blendMode), clipId,
                  materialId);
    return key.payload.circle;
  }

  LineCommand& EmplaceLine(unsigned int layer, float depth, const void* batchKey = nullptr,
                           octarine::BlendMode blendMode = octarine::BlendMode::Blend, std::uint8_t clipId = 0,
                           std::uint8_t materialId = 0) {
    auto& key = ClaimSlot();
    InitKeyHeader(key, layer, depth, LINE_PRIMITIVE, batchKey, static_cast<std::uint8_t>(blendMode), clipId,
                  materialId);
    return key.payload.line;
  }

  TriangleCommand& EmplaceTriangle(unsigned int layer, float depth, const void* batchKey = nullptr,
                                   octarine::BlendMode blendMode = octarine::BlendMode::Blend, std::uint8_t clipId = 0,
                                   std::uint8_t materialId = 0) {
    auto& key = ClaimSlot();
    InitKeyHeader(key, layer, depth, TRIANGLE_PRIMITIVE, batchKey, static_cast<std::uint8_t>(blendMode), clipId,
                  materialId);
    return key.payload.triangle;
  }

  CapsuleCommand& EmplaceCapsule(unsigned int layer, float depth, const void* batchKey = nullptr,
                                 octarine::BlendMode blendMode = octarine::BlendMode::Blend, std::uint8_t clipId = 0,
                                 std::uint8_t materialId = 0) {
    auto& key = ClaimSlot();
    InitKeyHeader(key, layer, depth, CAPSULE_PRIMITIVE, batchKey, static_cast<std::uint8_t>(blendMode), clipId,
                  materialId);
    return key.payload.capsule;
  }

  TileChunkCommand& EmplaceTileChunk(unsigned int layer, float depth, const void* batchKey = nullptr,
                                     octarine::BlendMode blendMode = octarine::BlendMode::Blend,
                                     std::uint8_t clipId = 0, std::uint8_t materialId = 0) {
    auto& key = ClaimSlot();
    InitKeyHeader(key, layer, depth, TILE_CHUNK, batchKey, static_cast<std::uint8_t>(blendMode), clipId, materialId);
    key.payload.tileChunk.blendMode = octarine::ToSdlBlendMode(blendMode);
    return key.payload.tileChunk;
  }

  std::uint8_t RegisterClipRect(const SDL_Rect& rect) {
    std::lock_guard<std::mutex> lock(clip_mutex_);
    const size_t currentCount = clip_count_.load(std::memory_order_relaxed);
    for (size_t i = 0; i < currentCount; ++i) {
      const auto& existing = clip_rects_[i];
      if (existing.x == rect.x && existing.y == rect.y && existing.w == rect.w && existing.h == rect.h) {
        return static_cast<std::uint8_t>(i + 1);
      }
    }
    if (currentCount >= kMaxClipRects) {
      Logger::Warn("RenderQueue: clip_rects_ capacity (255) exceeded; leaving draw unclipped.");
      return 0;
    }
    clip_rects_[currentCount] = rect;
    clip_count_.store(currentCount + 1, std::memory_order_release);
    return static_cast<std::uint8_t>(currentCount + 1);
  }

  [[nodiscard]] const SDL_Rect* GetClipRect(const std::uint8_t clipId) const noexcept {
    const size_t currentCount = clip_count_.load(std::memory_order_acquire);
    if (clipId == 0 || clipId > currentCount) return nullptr;
    return &clip_rects_[clipId - 1];
  }

  std::uint16_t RegisterGradient(const QuadGradient& gradient) {
    std::lock_guard<std::mutex> lock(gradient_mutex_);
    if (gradients_.size() >= static_cast<size_t>(std::numeric_limits<std::uint16_t>::max())) {
      return 0;
    }
    gradients_.push_back(gradient);
    return static_cast<std::uint16_t>(gradients_.size());
  }

  [[nodiscard]] const QuadGradient* GetGradient(const std::uint16_t gradientId) const noexcept {
    if (gradientId == 0 || gradientId > gradients_.size()) return nullptr;
    return &gradients_[gradientId - 1];
  }

  void Clear() {
    count_.store(0, std::memory_order_relaxed);
    overflow_warned_.store(false, std::memory_order_relaxed);
    clip_count_.store(0, std::memory_order_relaxed);
    gradients_.clear();
    ++clear_generation_;
  }

  [[nodiscard]] bool HasOverflowed() const noexcept { return overflow_warned_.load(std::memory_order_relaxed); }
  [[nodiscard]] std::uint64_t ClearGeneration() const noexcept { return clear_generation_; }

  void Sort() {
    const size_t n = std::min(count_.load(std::memory_order_relaxed), render_keys_.size());
    if (n <= 1) return;

    sort_entries_.resize(n);
    for (size_t i = 0; i < n; ++i) {
      sort_entries_[i].key = render_keys_[i].sortKey;
      sort_entries_[i].srcIdx = static_cast<std::uint32_t>(i);
    }

    RadixSort();

    permute_scratch_.resize(render_keys_.size());
    const auto* perm = sort_entries_.data();
    const auto* src = render_keys_.data();
    auto* dst = permute_scratch_.data();
    for (size_t i = 0; i < n; ++i) {
      dst[i] = src[perm[i].srcIdx];
    }
    std::swap(render_keys_, permute_scratch_);
  }

  [[nodiscard]] const_iterator begin() const noexcept { return render_keys_.cbegin(); }
  [[nodiscard]] const_iterator end() const noexcept {
    const size_t n = std::min(count_.load(std::memory_order_relaxed), render_keys_.size());
    return render_keys_.cbegin() + static_cast<std::ptrdiff_t>(n);
  }
  [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
  [[nodiscard]] const_iterator cend() const noexcept { return end(); }
  [[nodiscard]] bool IsEmpty() const noexcept { return count_.load(std::memory_order_relaxed) == 0; }
  [[nodiscard]] size_t Size() const noexcept {
    return std::min(count_.load(std::memory_order_relaxed), render_keys_.size());
  }

 private:
  void InitKeyHeader(RenderKey& key, const unsigned int layer, const float depth, const RenderableType type,
                     const void* batchKey, const std::uint8_t blendBits, const std::uint8_t clipId,
                     const std::uint8_t materialId, const std::uint16_t gradientId = 0) const noexcept {
    const LayerYSortConfig ySort = GetLayerYSort(layer);
    key.sortKey =
        RenderKey::ComputeSortKey(layer, depth, type, batchKey, blendBits, materialId, ySort.bandPx, ySort.enabled);
    key.type = type;
    key.clipId = clipId;
    key.materialId = materialId;
    key.gradientId = gradientId;
  }

  RenderKey& ClaimSlot() {
    const size_t i = count_.fetch_add(1, std::memory_order_relaxed);
    if (i >= render_keys_.size()) {
      if (!overflow_warned_.exchange(true, std::memory_order_relaxed)) {
        Logger::Warn("RenderQueue capacity (" + std::to_string(render_keys_.size()) +
                     ") exceeded; dropping excess render commands.");
      }
      thread_local RenderKey discardSlot{};
      return discardSlot;
    }
    return render_keys_[i];
  }

  struct SortEntry {
    std::uint64_t key;
    std::uint32_t srcIdx;
  };

  static bool IsUniformHistogram(const std::array<size_t, 256>& hist, const size_t n) noexcept {
    for (size_t bucket = 0; bucket < 256; ++bucket) {
      if (hist[bucket] == n) {
        return true;
      }
    }
    return false;
  }

  void RadixSort() {
    const size_t n = sort_entries_.size();
    if (n <= 1) return;
    radix_scratch_.resize(n);

    std::array<std::array<size_t, 256>, 8> histograms{};
    for (size_t i = 0; i < n; ++i) {
      const std::uint64_t k = sort_entries_[i].key;
      histograms[0][k & 0xFFu]++;
      histograms[1][(k >> 8) & 0xFFu]++;
      histograms[2][(k >> 16) & 0xFFu]++;
      histograms[3][(k >> 24) & 0xFFu]++;
      histograms[4][(k >> 32) & 0xFFu]++;
      histograms[5][(k >> 40) & 0xFFu]++;
      histograms[6][(k >> 48) & 0xFFu]++;
      histograms[7][(k >> 56) & 0xFFu]++;
    }

    std::array<bool, 8> skip{};
    for (size_t pass = 0; pass < 8; ++pass) {
      skip[pass] = IsUniformHistogram(histograms[pass], n);
    }

    std::vector<SortEntry>* in = &sort_entries_;
    std::vector<SortEntry>* out = &radix_scratch_;

    for (size_t pass = 0; pass < 8; ++pass) {
      if (skip[pass]) continue;

      const size_t shift = pass * 8;
      auto& counts = histograms[pass];
      size_t sum = 0;
      for (auto& c : counts) {
        const size_t saved = c;
        c = sum;
        sum += saved;
      }

      for (size_t i = 0; i < n; ++i) {
        const auto bucket = static_cast<std::uint8_t>(((*in)[i].key >> shift) & 0xFFu);
        (*out)[counts[bucket]++] = (*in)[i];
      }
      std::swap(in, out);
    }

    if (in != &sort_entries_) {
      std::swap(sort_entries_, radix_scratch_);
    }
  }

  std::vector<RenderKey> render_keys_{};
  std::atomic<size_t> count_{0};
  std::atomic<bool> overflow_warned_{false};

  std::vector<SortEntry> sort_entries_{};
  std::vector<SortEntry> radix_scratch_{};
  std::vector<RenderKey> permute_scratch_{};
  std::array<SDL_Rect, kMaxClipRects> clip_rects_{};
  std::atomic<size_t> clip_count_{0};
  mutable std::mutex clip_mutex_{};
  std::vector<QuadGradient> gradients_{};
  mutable std::mutex gradient_mutex_{};
  std::vector<LayerYSortConfig> layer_y_sort_{};
  std::uint64_t clear_generation_{0};
};
