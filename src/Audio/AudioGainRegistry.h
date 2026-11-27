#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

#include "EventBus/EventBus.h"
#include "Events/AudioPlayEvent.h"

namespace octarine {

// Applies per-cue gain and SFX volume scaling to unscaled AudioPlayEvents
// (event.volume ~= 1.0f) before AudioSystem::OnAudioPlay consumes them.
struct AudioGainRegistry {
  std::unordered_map<std::string, float> cue_gains{};
  float sfx_volume = 0.8F;
  bool muted = false;
  EventBus::SubscriptionHandle sub{};

  void Subscribe(EventBus& eventBus) {
    sub = eventBus.SubscribeEvent<AudioGainRegistry, AudioPlayEvent>(this, &AudioGainRegistry::OnAudioPlay);
  }

  void OnAudioPlay(AudioPlayEvent& event) {
    if (muted) {
      event.volume = 0.0F;
      return;
    }
    const auto it = cue_gains.find(event.clipId);
    if (it != cue_gains.end() && std::abs(event.volume - 1.0F) < 1e-4F) {
      event.volume = std::clamp(it->second * sfx_volume, 0.0F, 1.0F);
    }
  }
};

}  // namespace octarine
