#pragma once

#include <cmath>
#include <string>

#include "EventBus/Event.h"

struct AudioPlayEvent : Event {
  std::string clipId;
  float volume;
  bool applyMasterSfxVolume = true;

  AudioPlayEvent(std::string t_clipId, const float t_volume)
      : clipId(std::move(t_clipId)), volume(t_volume), applyMasterSfxVolume(std::abs(t_volume - 1.0F) < 1e-4F) {}

  AudioPlayEvent(std::string t_clipId, const float t_volume, const bool t_applyMasterSfxVolume)
      : clipId(std::move(t_clipId)), volume(t_volume), applyMasterSfxVolume(t_applyMasterSfxVolume) {}
};
