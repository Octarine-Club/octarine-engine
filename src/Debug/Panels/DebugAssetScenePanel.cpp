#include "Debug/Panels/DebugAssetScenePanel.h"

#ifdef OCTARINE_WITH_IMGUI
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <filesystem>
#include <string>
#include <vector>

#include "AssetManager/AssetManager.h"
#include "ECS/Registry.h"
#include "Engine/SceneLoader.h"
#include "Game/EngineContext.h"
#include "imgui.h"

namespace octarine::debug {

namespace {

struct AvailableScenesCache {
  float timer = 0.0f;
  std::string cachedBasePath{};
  std::vector<std::string> sceneRelativePaths{};
  std::vector<std::string> sceneFilenames{};
};

AvailableScenesCache& GetScenesCache() {
  static AvailableScenesCache cache;
  return cache;
}

void RescanScenesDirectory(AvailableScenesCache& cache, const std::string& basePath) {
  cache.cachedBasePath = basePath;
  cache.timer = 1.0f;  // Refresh directory scan every 1 second
  cache.sceneRelativePaths.clear();
  cache.sceneFilenames.clear();

  const std::filesystem::path scenesDir = std::filesystem::path(basePath) / "scripts" / "scenes";
  std::error_code ec;
  if (!std::filesystem::is_directory(scenesDir, ec)) return;

  for (const auto& entry : std::filesystem::directory_iterator(scenesDir, ec)) {
    if (entry.is_regular_file(ec) && entry.path().extension() == ".lua") {
      cache.sceneRelativePaths.push_back("scripts/scenes/" + entry.path().filename().string());
      cache.sceneFilenames.push_back(entry.path().filename().string());
    }
  }
}

void DrawAvailableScenes(const std::string& basePath, SceneLoader* loader) {
  auto& cache = GetScenesCache();
  const float dt = ImGui::GetIO().DeltaTime;
  cache.timer -= dt;

  if (cache.cachedBasePath != basePath || cache.timer <= 0.0f) {
    RescanScenesDirectory(cache, basePath);
  }

  ImGui::Separator();
  ImGui::TextDisabled("Available Scenes (scripts/scenes/):");
  ImGui::SameLine();
  if (ImGui::SmallButton("Scan Now")) {
    cache.timer = 0.0f;
  }

  for (size_t i = 0; i < cache.sceneRelativePaths.size(); ++i) {
    const auto& rel = cache.sceneRelativePaths[i];
    const auto& filename = cache.sceneFilenames[i];
    ImGui::PushID(rel.c_str());
    ImGui::TextUnformatted(filename.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Load") && loader != nullptr) {
      loader->RequestLoadScene(rel);
    }
    ImGui::PopID();
  }
}

void DrawScenesTab(Registry& registry, AssetManager* assets) {
  auto* loaderPtr = registry.TryGet<SceneLoader*>();
  SceneLoader* loader = loaderPtr ? *loaderPtr : nullptr;

  if (loader != nullptr) {
    ImGui::Text("Current Scene: %s", loader->GetCurrentScenePath().c_str());
    if (ImGui::Button("Reload Current Scene")) {
      loader->ReloadScene();
    }
  }

  ImGui::Separator();
  static char sceneInput[128] = "";
  ImGui::InputTextWithHint("##scene_path", "scripts/scenes/level1.lua", sceneInput, sizeof(sceneInput));
  ImGui::SameLine();
  if (ImGui::Button("Load Scene") && loader != nullptr && sceneInput[0] != '\0') {
    loader->RequestLoadScene(sceneInput);
  }

  if (assets != nullptr) {
    DrawAvailableScenes(assets->GetBasePath(), loader);
  }
}

void DrawTextureRow(const std::string& id, SDL_Texture* tex, AssetManager& assets, SDL_Renderer* renderer,
                    MIX_Mixer* mixer) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::TextUnformatted(id.c_str());

  float w = 0.0f;
  float h = 0.0f;
  if (tex != nullptr) {
    SDL_GetTextureSize(tex, &w, &h);
  }

  if (ImGui::IsItemHovered() && tex != nullptr) {
    ImGui::BeginTooltip();
    const auto texId = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(tex));
    ImGui::Image(texId, ImVec2(128.0f, 128.0f * (h > 0.0f ? h / w : 1.0f)));
    ImGui::EndTooltip();
  }

  ImGui::TableSetColumnIndex(1);
  ImGui::Text("%.0fx%.0f", static_cast<double>(w), static_cast<double>(h));

  ImGui::TableSetColumnIndex(2);
  ImGui::Text("%d", assets.RefCount(id));

  ImGui::TableSetColumnIndex(3);
  ImGui::PushID(id.c_str());
  if (ImGui::SmallButton("Reload")) {
    assets.Reload(id, renderer, mixer);
  }
  ImGui::PopID();
}

void DrawTexturesTab(Registry& registry, AssetManager* assets) {
  if (assets == nullptr) return;

  SDL_Renderer* renderer = registry.Get<EngineContext>().sdlRenderer;
  MIX_Mixer* mixer = registry.Get<EngineContext>().mixer;

  if (ImGui::BeginTable("##textures_table", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Refs", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableHeadersRow();

    for (const auto& [id, tex] : assets->GetTextures()) {
      DrawTextureRow(id, tex, *assets, renderer, mixer);
    }
    ImGui::EndTable();
  }
}

void DrawFontsTab(Registry& registry, AssetManager* assets) {
  if (assets == nullptr) return;

  SDL_Renderer* renderer = registry.Get<EngineContext>().sdlRenderer;
  MIX_Mixer* mixer = registry.Get<EngineContext>().mixer;

  if (ImGui::BeginTable("##fonts_table", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Base Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Refs", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableHeadersRow();

    for (const auto& [id, font] : assets->GetFonts()) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(id.c_str());

      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%.1f", static_cast<double>(assets->GetFontBaseSize(id)));

      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%d", assets->RefCount(id));

      ImGui::TableSetColumnIndex(3);
      ImGui::PushID(id.c_str());
      if (ImGui::SmallButton("Reload")) {
        assets->Reload(id, renderer, mixer);
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

void DrawAudioTab(Registry& registry, AssetManager* assets) {
  if (assets == nullptr) return;

  SDL_Renderer* renderer = registry.Get<EngineContext>().sdlRenderer;
  MIX_Mixer* mixer = registry.Get<EngineContext>().mixer;

  if (ImGui::BeginTable("##audio_table", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Refs", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableHeadersRow();

    for (const auto& [id, audio] : assets->GetAudioClips()) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(id.c_str());

      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%d", assets->RefCount(id));

      ImGui::TableSetColumnIndex(2);
      ImGui::PushID(id.c_str());
      if (ImGui::SmallButton("Play") && mixer != nullptr && audio != nullptr) {
        MIX_PlayAudio(mixer, audio);
      }

      ImGui::TableSetColumnIndex(3);
      if (ImGui::SmallButton("Reload")) {
        assets->Reload(id, renderer, mixer);
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

void DrawCatalogTab(AssetManager* assets) {
  if (assets == nullptr) return;

  static char catFilter[64] = "";
  ImGui::InputTextWithHint("##cat_filter", "Search catalog...", catFilter, sizeof(catFilter));

  if (ImGui::BeginTable("##catalog_all_table", 3,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 180.0f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    const std::string filter(catFilter);
    for (const auto& [id, entry] : assets->GetCatalog().Entries()) {
      if (!filter.empty() && id.find(filter) == std::string::npos && entry.fullPath.find(filter) == std::string::npos) {
        continue;
      }

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(id.c_str());

      ImGui::TableSetColumnIndex(1);
      static constexpr const char* kTypes[] = {"Texture", "Font", "Audio", "UIView", "UIStyle", "Tilemap"};
      const int typeIdx = static_cast<int>(entry.type);
      ImGui::TextUnformatted(typeIdx >= 0 && typeIdx < 6 ? kTypes[typeIdx] : "Unknown");

      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(entry.fullPath.c_str());
    }
    ImGui::EndTable();
  }
}

}  // namespace

void DrawAssetScenePanel(Registry& registry) {
  auto* assets = registry.TryGet<AssetManager>();

  if (ImGui::BeginTabBar("##assets_scenes_tabs")) {
    if (ImGui::BeginTabItem("Scenes")) {
      DrawScenesTab(registry, assets);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Textures")) {
      DrawTexturesTab(registry, assets);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Fonts")) {
      DrawFontsTab(registry, assets);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Audio")) {
      DrawAudioTab(registry, assets);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Catalog")) {
      DrawCatalogTab(assets);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
}

}  // namespace octarine::debug

#endif  // OCTARINE_WITH_IMGUI
