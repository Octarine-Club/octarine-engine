#pragma once

#ifndef OCTARINE_SHIPPED

#include <cstdint>
#include <memory>
#include <sol/sol.hpp>
#include <string>
#include <utility>
#include <vector>

#include "Components/UIRectComponent.h"
#include "ECS/Entity.h"

class Registry;

enum class PreviewBackdropMode : std::uint8_t {
  SceneOverlay = 0,
  DarkSlate = 1,
  LightGray = 2,
  Checkerboard = 3,
};

struct PreviewCatalogEntry {
  std::string id{};
  std::string fullPath{};
  std::string displayPath{};
};

struct UILayoutIssue {
  std::string code{};
  std::string severity{};
  Entity entity{};
  std::string label{};
  std::string message{};
  UIRectComponent rect{};
};

// Non-shipped UI previewer, canvas isolation, and deterministic layout audit service.
// Registered as a Registry singleton in Game::Setup() and stripped completely under OCTARINE_SHIPPED.
class UIPreviewer {
 public:
  explicit UIPreviewer(Registry* registry = nullptr);
  ~UIPreviewer();

  UIPreviewer(const UIPreviewer&) = delete;
  UIPreviewer& operator=(const UIPreviewer&) = delete;
  UIPreviewer(UIPreviewer&&) noexcept;
  UIPreviewer& operator=(UIPreviewer&&) noexcept;

  void SetRegistry(Registry* registry) noexcept { registry_ = registry; }
  void RefreshCatalogViews();

  [[nodiscard]] const std::vector<PreviewCatalogEntry>& CatalogViews() const noexcept { return catalog_views_; }
  [[nodiscard]] const std::vector<PreviewCatalogEntry>& CatalogStyles() const noexcept { return catalog_styles_; }

  bool MountPreview(const std::string& pathOrId, const sol::optional<sol::table>& opts = sol::nullopt);
  bool MountPreviewWithMock(const std::string& pathOrId, const std::string& mockScriptPath);
  bool MountInlineXml(const std::string& xmlSource, const std::string& ossSource = "");
  bool ReloadActivePreview();
  void UnmountPreview(bool restoreIsolationState = true);

  [[nodiscard]] bool IsActive() const;
  [[nodiscard]] bool IsInline() const noexcept { return is_inline_; }
  [[nodiscard]] const std::string& ActiveSource() const noexcept { return active_source_; }
  [[nodiscard]] const std::string& ActiveMockPath() const noexcept { return active_mock_path_; }
  [[nodiscard]] Entity ActiveRootEntity() const;
  [[nodiscard]] std::uint64_t ActiveViewId() const noexcept { return active_view_id_; }

  void SetBackdropMode(PreviewBackdropMode mode);
  [[nodiscard]] PreviewBackdropMode GetBackdropMode() const noexcept { return backdrop_mode_; }

  void SetIsolatedMode(bool isolated, bool managePauseState = true);
  [[nodiscard]] bool IsIsolatedMode() const noexcept { return isolated_; }
  [[nodiscard]] bool ShouldRenderWhilePaused() const;

  void SyncActivePreview();
  void EmitBackdropIfNeeded(Registry& registry);
  void RenderWhenPaused(Registry& registry);

  [[nodiscard]] std::vector<UILayoutIssue> AuditLayout() const;
  [[nodiscard]] std::string AuditLayoutJson() const;

  [[nodiscard]] const std::vector<std::string>& RecentCallbackEvents() const noexcept { return callback_events_; }
  void RecordCallbackEvent(const std::string& eventText);
  void ClearCallbackEvents() noexcept { callback_events_.clear(); }

  void RequestCapture(std::string path) { pending_capture_path_ = std::move(path); }
  [[nodiscard]] bool HasPendingCapture() const noexcept { return !pending_capture_path_.empty(); }
  [[nodiscard]] std::string ConsumePendingCapturePath() {
    std::string out = std::move(pending_capture_path_);
    pending_capture_path_.clear();
    return out;
  }
  void SetLastCaptureStatus(std::string status) { last_capture_status_ = std::move(status); }
  [[nodiscard]] const std::string& LastCaptureStatus() const noexcept { return last_capture_status_; }

 private:
  struct PausedRenderSystems;

  [[nodiscard]] sol::table BuildFallbackController(const sol::optional<sol::table>& userController);
  [[nodiscard]] sol::optional<sol::table> PrepareMountOptions(const sol::optional<sol::table>& opts,
                                                              const std::string& inlineOss);
  [[nodiscard]] bool LoadMockScriptOptions(const std::string& mockPath, sol::table& outOpts);
  void SyncPreviewLayerOrder();
  void RestoreBackgroundIsolation();

  Registry* registry_{nullptr};
  std::unique_ptr<PausedRenderSystems> paused_systems_;

  std::vector<PreviewCatalogEntry> catalog_views_{};
  std::vector<PreviewCatalogEntry> catalog_styles_{};

  Entity active_root_{};
  std::uint64_t active_view_id_{0};
  int orig_canvas_base_layer_{0};
  bool captured_orig_base_layer_{false};
  int backdrop_layer_{0};

  std::string active_source_{};
  std::string active_mock_path_{};
  std::string inline_xml_{};
  std::string inline_oss_{};
  bool is_inline_{false};

  PreviewBackdropMode backdrop_mode_{PreviewBackdropMode::SceneOverlay};
  bool isolated_{false};
  bool prev_paused_{false};
  bool has_saved_pause_state_{false};
  std::vector<std::pair<std::uint64_t, bool>> muted_view_states_{};
  std::vector<std::string> callback_events_{};
  std::string pending_capture_path_{};
  std::string last_capture_status_{};
};

#endif  // !OCTARINE_SHIPPED
