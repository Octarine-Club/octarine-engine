#pragma once

#include <SDL3_mixer/SDL_mixer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "EventBus/EventBus.h"
#include "Events/AudioPlayEvent.h"

namespace octarine {

struct AudioClipLimit {
  int maxVoices = 8;
  float minIntervalSec = 0.0F;
};

// Applies per-cue gain and SFX volume scaling to AudioPlayEvents that opt into master SFX scaling
// (event.applyMasterSfxVolume == true) before AudioSystem::OnAudioPlay consumes them, and holds
// per-clip concurrency/cooldown limits enforced by AudioSystem::OnAudioPlay.
struct AudioGainRegistry {
  std::unordered_map<std::string, float> cue_gains{};
  std::unordered_map<std::string, AudioClipLimit> clip_limits{};
  std::unordered_map<std::string, std::uint64_t> last_play_ticks_ns{};
  std::unordered_map<MIX_Track*, std::string> active_track_clips{};
  int default_max_voices = 8;
  float default_min_interval_sec = 0.0F;
  float sfx_volume = 0.8F;
  bool muted = false;
  EventBus::SubscriptionHandle sub{};

  void Subscribe(EventBus& eventBus) {
    sub = eventBus.SubscribeEvent<AudioGainRegistry, AudioPlayEvent>(this, &AudioGainRegistry::OnAudioPlay);
  }

  [[nodiscard]] AudioClipLimit ResolveClipLimit(const std::string& clipId) const {
    if (const auto it = clip_limits.find(clipId); it != clip_limits.end()) {
      return it->second;
    }
    return AudioClipLimit{default_max_voices, default_min_interval_sec};
  }

  [[nodiscard]] bool IsWithinCooldown(const std::string& clipId, const std::uint64_t nowNs,
                                      const float minIntervalSec) const {
    if (minIntervalSec <= 0.0F) return false;
    const auto it = last_play_ticks_ns.find(clipId);
    if (it == last_play_ticks_ns.end() || nowNs < it->second) return false;
    const auto minDeltaNs = static_cast<std::uint64_t>(static_cast<double>(minIntervalSec) * 1.0e9);
    return (nowNs - it->second) < minDeltaNs;
  }

  [[nodiscard]] bool ShouldThrottleClip(const std::string& clipId, const std::uint64_t nowNs,
                                        const int activeVoices) const {
    const AudioClipLimit limit = ResolveClipLimit(clipId);
    if (IsWithinCooldown(clipId, nowNs, limit.minIntervalSec)) {
      return true;
    }
    return limit.maxVoices > 0 && activeVoices >= limit.maxVoices;
  }

  void RecordClipPlayed(MIX_Track* track, const std::string& clipId, const std::uint64_t nowNs) {
    last_play_ticks_ns[clipId] = nowNs;
    if (track != nullptr) {
      active_track_clips[track] = clipId;
    }
  }

  void OnAudioPlay(AudioPlayEvent& event) {
    if (muted) {
      event.volume = 0.0F;
      return;
    }
    if (!event.applyMasterSfxVolume) {
      return;
    }
    const auto it = cue_gains.find(event.clipId);
    const float cueGain = (it != cue_gains.end()) ? it->second : 1.0F;
    event.volume = std::clamp(event.volume * cueGain * sfx_volume, 0.0F, 1.0F);
  }
};

}  // namespace octarine
