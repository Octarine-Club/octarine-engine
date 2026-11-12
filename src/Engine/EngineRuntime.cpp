#include "Engine/EngineRuntime.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <string>

#include "General/Icons/OctarineIcon.h"
#include "General/Logger.h"

#ifdef OCTARINE_WITH_IMGUI
#include "Debug/DebugTheme.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#endif

bool EngineRuntime::InitSubsystems(const bool headless) {
  if (headless) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
  }

  constexpr auto SDL_INI = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD;

  if (!SDL_Init(SDL_INI)) {
    Logger::Error("SDL_Init Error: " + std::string(SDL_GetError()));
    return false;
  }

  if (!TTF_Init()) {
    Logger::Error("TTF_Init Error: " + std::string(SDL_GetError()));
    return false;
  }

  return true;
}

bool EngineRuntime::CreateWindow(const std::string& title, int width, int height, const bool headless,
                                 const bool resizable) {
  SDL_WindowFlags windowFlags = 0;
  if (headless) {
    windowFlags = SDL_WINDOW_HIDDEN;
  } else {
    windowFlags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (resizable) {
      windowFlags |= SDL_WINDOW_RESIZABLE;
    }
  }
  SDL_CreateWindowAndRenderer(title.c_str(), width, height, windowFlags, &window_, &sdl_renderer_);

  if (!window_) {
    Logger::Error("SDL_CreateWindow Error: " + std::string(SDL_GetError()));
    return false;
  }

  if (!sdl_renderer_) {
    Logger::Error("SDL_CreateRenderer Error: " + std::string(SDL_GetError()));
    return false;
  }

  if (!headless) {
    SetWindowIcon(octarine::icons::kOctarineIconPng, octarine::icons::kOctarineIconPngSize);
  }

  return true;
}

bool EngineRuntime::SetWindowIcon(const void* pngData, const size_t pngSize) {
  if (!window_ || !pngData || pngSize == 0) {
    return false;
  }

  SDL_IOStream* io = SDL_IOFromConstMem(pngData, pngSize);
  if (!io) {
    return false;
  }

  SDL_Surface* surface = IMG_Load_IO(io, true);
  if (!surface) {
    Logger::Warn("Failed to decode window icon PNG: " + std::string(SDL_GetError()));
    return false;
  }

  const bool ok = SDL_SetWindowIcon(window_, surface);
  SDL_DestroySurface(surface);
  return ok;
}

#ifdef OCTARINE_WITH_IMGUI
void EngineRuntime::InitImGui(const char* iniPath) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  octarine::debug::ApplyOctarineTheme();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = iniPath;

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  ImGui_ImplSDL3_InitForSDLRenderer(window_, sdl_renderer_);
  ImGui_ImplSDLRenderer3_Init(sdl_renderer_);
}
#endif

void EngineRuntime::Shutdown() {
  if (sdl_renderer_) {
#ifdef OCTARINE_WITH_IMGUI
    if (ImGui::GetCurrentContext() != nullptr) {
      ImGui_ImplSDLRenderer3_Shutdown();
      ImGui_ImplSDL3_Shutdown();
      ImGui::DestroyContext();
    }
#endif
    SDL_DestroyRenderer(sdl_renderer_);
    sdl_renderer_ = nullptr;
  }

  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }

  SDL_Quit();
}
