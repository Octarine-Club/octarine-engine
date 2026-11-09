#include "Systems/TilemapDebugPanel.h"

#ifdef OCTARINE_WITH_IMGUI

#include <cstdint>

#include "ECS/Registry.h"
#include "Systems/TilemapSystem.h"
#include "Tilemap/ChunkTemplate.h"
#include "imgui.h"

namespace octarine::systems {

void DrawTilemapDebugPanel(Registry* registry) {
  if (registry == nullptr) {
    return;
  }
  auto* tilemap = registry->TryGet<TilemapSystem>();
  if (tilemap == nullptr || !tilemap->IsLoaded()) {
    return;
  }

  if (!ImGui::Begin("Tilemap Inspector", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }

  const auto& formatStr = tilemap->GetTilemapData().sourceFormat;
  ImGui::Text("Source (%s): %s", formatStr.empty() ? "custom" : formatStr.c_str(), tilemap->GetLoadedPath().c_str());
  ImGui::Text("Templates: %zu | Active Chunks: %zu", tilemap->GetTemplateCount(), tilemap->GetActiveChunkCount());
  ImGui::Text("Chunk Grid: %dx%d tiles (%dx%d px)", tilemap->GetChunkGridWidth(), tilemap->GetChunkGridHeight(),
              tilemap->GetChunkPixelWidth(), tilemap->GetChunkPixelHeight());
  ImGui::Text("Camera Chunk: (%d, %d) | Hot-Reloads: %llu", tilemap->GetCameraChunkX(), tilemap->GetCameraChunkY(),
              static_cast<unsigned long long>(tilemap->GetHotReloadCount()));

  ImGui::Separator();

  bool showBounds = tilemap->GetShowChunkBounds();
  if (ImGui::Checkbox("Show Chunk Boundaries", &showBounds)) {
    tilemap->SetShowChunkBounds(showBounds);
  }

  bool showCollisions = tilemap->GetShowCollisionOverlay();
  if (ImGui::Checkbox("Show Collisions IntGrid Overlay", &showCollisions)) {
    tilemap->SetShowCollisionOverlay(showCollisions);
  }

  ImGui::Separator();

  static constexpr const char* kModeLabels[] = {"Toroidal", "ProceduralHash", "ArenaFixed"};
  int modeIdx = static_cast<int>(tilemap->GetStitchMode());
  if (ImGui::Combo("Stitch Mode", &modeIdx, kModeLabels, 3)) {
    tilemap->SetStitchMode(*registry, static_cast<octarine::tilemap::StitchMode>(modeIdx), tilemap->GetSeed());
  }

  int seedVal = static_cast<int>(tilemap->GetSeed());
  if (ImGui::InputInt("Stitch Seed", &seedVal)) {
    tilemap->SetStitchMode(*registry, tilemap->GetStitchMode(), static_cast<uint32_t>(seedVal));
  }

  const bool canReload = !tilemap->GetLoadedPath().empty() && tilemap->GetLoadedPath().front() != '<';
  ImGui::BeginDisabled(!canReload);
  if (ImGui::Button("Reload Tilemap Now")) {
    tilemap->Reload(*registry);
  }
  ImGui::EndDisabled();

  ImGui::End();
}

}  // namespace octarine::systems

#endif  // OCTARINE_WITH_IMGUI
