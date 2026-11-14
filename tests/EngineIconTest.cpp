#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <cstdint>
#include <iostream>
#include <vector>

#include "Engine/EngineRuntime.h"
#include "General/Icons/OctarineIcon.h"
#include "TestHarness.h"

using octarine::test::Check;
using octarine::test::CheckEq;

int main() {
  std::cout << "[EngineIconTest] Starting icon verification...\n";

  // Check 1: Embedded icon constant properties
  CheckEq(octarine::icons::kOctarineIconPngSize, static_cast<unsigned int>(1580), "Embedded icon size is 1580 bytes");
  Check(octarine::icons::kOctarineIconPng[0] == 0x89, "Embedded icon begins with PNG magic byte");

  // Check 2: SDL3_image decodes the embedded PNG as a 64x64 surface
  SDL_IOStream* io = SDL_IOFromConstMem(octarine::icons::kOctarineIconPng, octarine::icons::kOctarineIconPngSize);
  Check(io != nullptr, "SDL_IOFromConstMem created io stream");
  if (io != nullptr) {
    SDL_Surface* surface = IMG_Load_IO(io, true);
    Check(surface != nullptr, "IMG_Load_IO decoded embedded icon PNG");
    if (surface != nullptr) {
      CheckEq(surface->w, 64, "Icon surface width is 64");
      CheckEq(surface->h, 64, "Icon surface height is 64");
      SDL_DestroySurface(surface);
    }
  }

  // Check 3: EngineRuntime SetWindowIcon behavior
  EngineRuntime runtime;
  Check(!runtime.SetWindowIcon(octarine::icons::kOctarineIconPng, octarine::icons::kOctarineIconPngSize),
        "SetWindowIcon fails gracefully before window creation");

  const bool inited = runtime.InitSubsystems(/*headless=*/true);
  Check(inited, "Runtime subsystems initialized (headless)");

  const bool created = runtime.CreateWindow("IconTest", 640, 480, /*headless=*/true);
  Check(created, "Headless window created");

  const bool iconSet = runtime.SetWindowIcon(octarine::icons::kOctarineIconPng, octarine::icons::kOctarineIconPngSize);
  const std::string err = SDL_GetError();
  // On real display backends (Cocoa/Win32/Wayland/X11), SDL_SetWindowIcon succeeds.
  // On SDL's dummy headless driver, the display backend reports "That operation is not supported".
  Check(iconSet || err == "That operation is not supported",
        "SetWindowIcon succeeds or gracefully reports headless dummy unsupported status");
  Check(!runtime.SetWindowIcon(nullptr, 0), "SetWindowIcon rejects null pointer");
  Check(!runtime.SetWindowIcon("not_a_valid_png", 14), "SetWindowIcon rejects invalid image payload");

  // Check 4: Oversized (>256x256) icon downscaling path succeeds without error
  SDL_Surface* largeSurf = SDL_CreateSurface(512, 512, SDL_PIXELFORMAT_RGBA32);
  Check(largeSurf != nullptr, "Created 512x512 test surface");
  if (largeSurf != nullptr) {
    SDL_ClearSurface(largeSurf, 0.25f, 0.5f, 0.75f, 1.0f);
    std::vector<std::uint8_t> pngBuf(512u * 512u * 4u);
    SDL_IOStream* memIo = SDL_IOFromMem(pngBuf.data(), pngBuf.size());
    const bool saved = memIo != nullptr && IMG_SavePNG_IO(largeSurf, memIo, false);
    const Sint64 pngBytes = memIo != nullptr ? SDL_TellIO(memIo) : 0;
    if (memIo != nullptr) {
      SDL_CloseIO(memIo);
    }
    SDL_DestroySurface(largeSurf);
    Check(saved && pngBytes > 0, "Encoded 512x512 test PNG into memory buffer");
    if (saved && pngBytes > 0) {
      const bool largeSet = runtime.SetWindowIcon(pngBuf.data(), static_cast<size_t>(pngBytes));
      const std::string largeErr = SDL_GetError();
      Check(largeSet || largeErr == "That operation is not supported",
            "Oversized 512x512 SetWindowIcon downscales cleanly");
    }
  }

  runtime.Shutdown();

  return octarine::test::ReportSummary("EngineIconTest");
}
