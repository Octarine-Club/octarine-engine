#include "Lua/Modules/AudioModuleLuaBinding.h"

#include <algorithm>
#include <string>

#include "Audio/AudioGainRegistry.h"
#include "ECS/Registry.h"
#include "EventBus/EventBus.h"
#include "Events/AudioPlayEvent.h"
#include "Game/EngineContext.h"
#include "General/Logger.h"
#include "Lua/LuaBindingContext.h"
#include "Lua/LuaSchemaRegistry.h"

namespace {
using octarine::lua_schema::LuaSchemaRegistry;

EventBus* TryGetEventBus(LuaBindingContext& ctx) {
  if (const Registry* registry = ctx.GetRegistry()) {
    if (const auto* ectx = registry->TryGet<EngineContext>()) {
      return ectx->eventBus;
    }
  }
  return nullptr;
}

octarine::AudioGainRegistry* EnsureAudioGainRegistry(LuaBindingContext& ctx) {
  auto* registry = ctx.GetRegistry();
  if (registry == nullptr) return nullptr;
  if (auto* gainReg = registry->TryGet<octarine::AudioGainRegistry>()) {
    return gainReg;
  }
  auto& gainReg = registry->Set<octarine::AudioGainRegistry>(octarine::AudioGainRegistry{});
  if (auto* eventBus = TryGetEventBus(ctx)) {
    gainReg.Subscribe(*eventBus);
  }
  return &gainReg;
}
}  // namespace

void LuaModuleBinding<AudioModule>::install(sol::state& lua, LuaBindingContext& ctx) {
  LuaSchemaRegistry::SetFunction(lua, "play_sound",
                                 [&ctx](const std::string& clipId, const sol::optional<float> volume) {
                                   auto* eventBus = TryGetEventBus(ctx);
                                   if (!eventBus) {
                                     Logger::Error("play_sound called before event bus is ready");
                                     return;
                                   }
                                   eventBus->EmitEvent<AudioPlayEvent>(clipId, volume.value_or(1.0F), true);
                                 });

  LuaSchemaRegistry::SetFunction(lua, "set_audio_cue_gain", [&ctx](const std::string& clipId, const float gain) {
    if (clipId.empty()) return;
    if (auto* gainReg = EnsureAudioGainRegistry(ctx)) {
      gainReg->cue_gains[clipId] = std::clamp(gain, 0.0F, 1.0F);
    }
  });

  LuaSchemaRegistry::SetFunction(lua, "set_native_sfx_volume",
                                 [&ctx](const float sfxVolume, const sol::optional<bool> muted) {
                                   if (auto* gainReg = EnsureAudioGainRegistry(ctx)) {
                                     gainReg->sfx_volume = std::clamp(sfxVolume, 0.0F, 1.0F);
                                     if (muted.has_value()) {
                                       gainReg->muted = *muted;
                                     }
                                   }
                                 });

  LuaSchemaRegistry::SetFunction(
      lua, "set_audio_clip_limits",
      [&ctx](const std::string& clipId, const int maxVoices, const sol::optional<float> minIntervalSec) {
        if (clipId.empty()) return;
        auto* gainReg = EnsureAudioGainRegistry(ctx);
        if (gainReg == nullptr) return;
        const int clampedVoices = std::max(0, maxVoices);
        const float clampedInterval = std::max(0.0F, minIntervalSec.value_or(0.0F));
        if (clipId == "*") {
          gainReg->default_max_voices = clampedVoices;
          gainReg->default_min_interval_sec = clampedInterval;
        } else {
          gainReg->clip_limits[clipId] = octarine::AudioClipLimit{clampedVoices, clampedInterval};
        }
      });
}
