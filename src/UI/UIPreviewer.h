#pragma once

#ifndef OCTARINE_SHIPPED

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <sol/sol.hpp>
#include <string>
#include <utility>
#include <vector>

#include "Components/UIRectComponent.h"
#include "ECS/Entity.h"
#include "UI/OxmlContract.h"

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

struct PreviewMatrixOptions {
  std::string outDir{};
  std::vector<std::pair<int, int>> resolutions{};
  std::vector<std::string> presets{};
};

struct PreviewMatrixCellResult {
  std::string preset{};
  int width{0};
  int height{0};
  float uiScale{1.0f};
  std::string capturePath{};
  std::size_t issueCount{0};
  bool captureOk{false};
  std::vector<UILayoutIssue> issues{};
};

struct PreviewMatrixResult {
  bool ok{false};
  std::string outDir{};
  std::string reportPath{};
  std::string reportJson{};
  std::vector<PreviewMatrixCellResult> cells{};
};

class UIPreviewer {
 public:
  using CaptureCallback = std::function<bool(const std::string&)>;

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
  bool MountPreviewWithMock(const std::string& pathOrId, const std::string& mockScriptPath,
                            const std::string& presetName = "");
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

  void SetSimulatedViewport(int width, int height);
  void ClearSimulatedViewport();
  [[nodiscard]] bool HasSimulatedViewport() const noexcept { return sim_viewport_w_ > 0 && sim_viewport_h_ > 0; }
  [[nodiscard]] int SimulatedWidth() const noexcept { return sim_viewport_w_; }
  [[nodiscard]] int SimulatedHeight() const noexcept { return sim_viewport_h_; }
  [[nodiscard]] std::pair<int, int> SimulatedViewport() const noexcept { return {sim_viewport_w_, sim_viewport_h_}; }
  void SetShowSafeAreaGuides(bool enabled) noexcept { show_safe_area_guides_ = enabled; }
  [[nodiscard]] bool ShowSafeAreaGuides() const noexcept { return show_safe_area_guides_; }
  void SetIntegerZoomSimulation(bool enabled) noexcept { integer_zoom_sim_ = enabled; }
  [[nodiscard]] bool IntegerZoomSimulation() const noexcept { return integer_zoom_sim_; }
  void SetCaptureCallback(CaptureCallback cb) { capture_callback_ = std::move(cb); }
  [[nodiscard]] bool HasCaptureCallback() const noexcept { return static_cast<bool>(capture_callback_); }
  [[nodiscard]] std::string ViewportInfoJson() const;

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

  [[nodiscard]] std::vector<std::string> ListMockPresets() const;
  [[nodiscard]] const std::string& ActiveMockPreset() const noexcept { return active_mock_preset_; }
  [[nodiscard]] std::string DefaultMockPreset() const;
  [[nodiscard]] bool HasLiveControllerScript() const noexcept { return has_live_mock_controller_; }
  bool SelectMockPreset(const std::string& presetName);
  bool SaveMockPreset(const std::string& presetName, bool setAsDefault = false);
  bool DeleteMockPreset(const std::string& presetName);
  bool ApplyStressPreset(const std::string& kind);
  [[nodiscard]] std::string ListPresetsJson() const;

  [[nodiscard]] PreviewMatrixResult RunPreviewMatrix(const PreviewMatrixOptions& opts);
  [[nodiscard]] std::string RunPreviewMatrixJson(const std::string& outDir, const std::string& resolutionsCsv = "",
                                                 const std::string& presetsCsv = "");
  [[nodiscard]] static std::vector<std::pair<int, int>> ParseResolutionsCsv(const std::string& csv);

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
  int sim_viewport_w_{0};
  int sim_viewport_h_{0};
  int saved_unsimulated_w_{0};
  int saved_unsimulated_h_{0};
  bool has_saved_unsimulated_size_{false};
  bool show_safe_area_guides_{false};
  bool integer_zoom_sim_{false};
  std::string active_mock_preset_{};
  bool has_live_mock_controller_{false};
  CaptureCallback capture_callback_{};

  bool MountResolvedPreview(std::string sourceIn, const sol::optional<sol::table>& opts, std::string inlineOssIn,
                            bool isInline, MockSourceMode mockMode, std::string mockPathIn, std::string sidecarPathIn,
                            const sol::optional<sol::table>& rawMockRoot, std::string presetName = "");
  bool MountSidecarPreview(const std::string& resolvedSource, const std::string& sidecarPath,
                           const std::string& requestedPreset, const sol::optional<sol::table>& opts);
  void RefreshSidecarMetadataAfterSave(const std::string& dest);
  void ApplyAuditIssuesForContract(std::vector<UILayoutIssue>& out) const;
  void RestoreSimulatedViewportIfOwned();
  [[nodiscard]] std::string ExportMockLuaScriptForDoc(const sol::table& docTbl, const std::string& targetPresetKey,
                                                      const std::string& defaultPresetOverride,
                                                      bool promoteFlatIfNeeded, bool* outHasPresets = nullptr) const;
};

#endif  // !OCTARINE_SHIPPED
