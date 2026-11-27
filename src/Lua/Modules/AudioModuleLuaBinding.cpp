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

namespace {
octarine::AudioGainRegistry* EnsureAudioGainRegistry(LuaBindingContext& ctx) {
  auto* registry = ctx.GetRegistry();
  if (registry == nullptr) return nullptr;
  if (auto* gainReg = registry->TryGet<octarine::AudioGainRegistry>()) {
    return gainReg;
  }
  auto& gainReg = registry->Set<octarine::AudioGainRegistry>(octarine::AudioGainRegistry{});
  if (auto* eventBus = ctx.GetContext().eventBus) {
    gainReg.Subscribe(*eventBus);
  }
  return &gainReg;
}
}  // namespace

void LuaModuleBinding<AudioModule>::install(sol::state& lua, LuaBindingContext& ctx) {
  lua.set_function("play_sound", [&ctx](const std::string& clipId, const sol::optional<float> volume) {
    auto* eventBus = ctx.GetContext().eventBus;
    if (!eventBus) {
      Logger::Error("play_sound called before event bus is ready");
      return;
    }
    eventBus->EmitEvent<AudioPlayEvent>(clipId, volume.value_or(1.0F));
  });

  lua.set_function("set_audio_cue_gain", [&ctx](const std::string& clipId, const float gain) {
    if (clipId.empty()) return;
    if (auto* gainReg = EnsureAudioGainRegistry(ctx)) {
      gainReg->cue_gains[clipId] = std::clamp(gain, 0.0F, 1.0F);
    }
  });

  lua.set_function("set_native_sfx_volume", [&ctx](const float sfxVolume, const sol::optional<bool> muted) {
    if (auto* gainReg = EnsureAudioGainRegistry(ctx)) {
      gainReg->sfx_volume = std::clamp(sfxVolume, 0.0F, 1.0F);
      if (muted.has_value()) {
        gainReg->muted = *muted;
      }
    }
  });
}
