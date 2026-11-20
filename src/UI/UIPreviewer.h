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

#include <filesystem>

#include "UI/OxmlContract.h"

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
enum class MockSourceMode : std::uint8_t {
  None = 0,
  ExplicitLua = 1,
  SidecarPreview = 2,
  AutoSeeded = 3,
};

enum class MockFieldKind : std::uint8_t {
  String = 0,
  Number = 1,
  Boolean = 2,
  Array = 3,
  Table = 4,
};

struct MockFieldEntry {
  std::string path{};
  MockFieldKind kind{MockFieldKind::String};
  std::string stringValue{};
  double numberValue{0.0};
  bool boolValue{false};
  int arrayLength{0};
  bool isUnset{false};
  bool isAutoSeeded{false};
  bool isConditionalOnly{false};
  bool isTwoWay{false};
  std::string sampleHint{};
  float sliderMin{0.0f};
  float sliderMax{1.0f};
  float sliderStep{0.0f};
  bool hasSliderRange{false};
  std::vector<std::string> radioOptions{};
  std::string arrayItemTemplate{};
};

struct MockParamEntry {
  std::string name{};
  std::string defaultValue{};
  std::string currentValue{};
  bool isOverridden{false};
};

struct MockCallbackEntry {
  std::string name{};
  std::string sourceAttr{};
  std::string rawExpr{};
  bool hasControllerHandler{false};
};

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

  [[nodiscard]] const OxmlContract& ActiveContract() const noexcept { return active_contract_; }
  [[nodiscard]] MockSourceMode GetMockSourceMode() const noexcept { return mock_source_mode_; }
  [[nodiscard]] const std::string& TargetSidecarPath() const noexcept { return target_sidecar_path_; }
  [[nodiscard]] bool IsMockModified() const noexcept { return mock_modified_; }
  [[nodiscard]] const std::string& LastMockError() const noexcept { return last_mock_error_; }
  void ClearLastMockError() noexcept { last_mock_error_.clear(); }

  [[nodiscard]] std::vector<MockFieldEntry> CollectMockFields() const;
  [[nodiscard]] std::vector<MockParamEntry> CollectMockParams() const;
  [[nodiscard]] std::vector<MockCallbackEntry> CollectMockCallbacks() const;

  bool SetMockFieldString(const std::string& path, const std::string& value);
  bool SetMockFieldNumber(const std::string& path, double value);
  bool SetMockFieldBool(const std::string& path, bool value);
  bool SetMockFieldObject(const std::string& path, const sol::object& value);
  bool ResizeMockArray(const std::string& path, int newLength);
  bool SeedSingleContractField(const std::string& path);
  bool SeedMissingContractFields();
  bool SetMockParam(const std::string& name, const std::string& value);
  bool TriggerMockCallback(const std::string& name);

  bool LoadMockScript(const std::string& mockPath);
  bool ResetToAutoSeeded();
  bool SaveMockSidecar(const std::string& path = "");
  [[nodiscard]] std::string ExportMockLuaScript() const;
  [[nodiscard]] std::string InspectContractJson(const std::string& pathOrId = "") const;
  void PollMockHotReload(float deltaTime, float pollIntervalSeconds = 0.25f);
  void RefreshContractState();
  [[nodiscard]] static bool IsContractFieldUnset(const ContractField& f, const std::vector<MockFieldEntry>& liveFields);

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
  std::shared_ptr<UIPreviewer*> self_handle_{};

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
  MockSourceMode mock_source_mode_{MockSourceMode::None};
  std::string target_sidecar_path_{};
  std::string last_mock_error_{};
  bool mock_modified_{false};
  std::filesystem::file_time_type last_mock_write_time_{};
  bool has_mock_write_time_{false};
  float mock_hot_reload_accum_{0.0f};
  std::uint64_t last_seen_reload_count_{0};
  std::uint64_t callback_seq_{0};
  OxmlContract active_contract_{};

  bool MountResolvedPreview(const std::string& sourceCopy, const sol::optional<sol::table>& opts,
                            const std::string& inlineOss, bool isInline, MockSourceMode mockMode,
                            const std::string& mockPath, const std::string& sidecarPath,
                            const sol::optional<sol::table>& rawMockRoot);
  void ApplyAuditIssuesForContract(std::vector<UILayoutIssue>& out) const;
};

#endif  // !OCTARINE_SHIPPED
