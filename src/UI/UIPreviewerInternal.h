#pragma once

#ifndef OCTARINE_SHIPPED

#include <cstdint>
#include <filesystem>
#include <sol/sol.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "UI/OxmlContract.h"
#include "UI/UIPreviewer.h"

class Registry;
class UIRuntime;

namespace octarine::ui_preview_detail {

enum class MockSeedVariant : std::uint8_t {
  Compact = 0,
  Empty = 1,
  Overflow = 2,
};

[[nodiscard]] std::string EscapeJsonString(std::string_view input);
[[nodiscard]] const char* MockSourceModeName(MockSourceMode mode);
[[nodiscard]] std::string ReadFileOrAssetText(UIRuntime* uiRuntime, const std::string& pathOrId, std::string& outErr);
[[nodiscard]] std::string ResolveMockDiskPath(const Registry* registry, const UIRuntime* uiRuntime,
                                              const std::string& mockPath);

[[nodiscard]] bool IsObservableInternalKey(std::string_view key);
[[nodiscard]] sol::table UnwrapObservableShallow(const sol::table& tbl);
[[nodiscard]] sol::object UnwrapObservableDataDeep(sol::state& lua, const sol::object& val);
[[nodiscard]] bool ContainsFunctionValue(const sol::table& tbl, int depth = 0);
[[nodiscard]] bool HasAnyControllerFunction(const sol::table& rawRoot, const sol::table& activeCtrl);
void PruneEmptyControllerTable(sol::table& tbl);

void PinLuaRegistryObject(lua_State* L, const char* registryKey, const sol::object& obj);
[[nodiscard]] sol::object GetPinnedLuaRegistryObject(lua_State* L, const char* registryKey);
bool TryGetActiveViewContext(const Registry* registry, sol::state*& outLua, sol::table& outViewTbl,
                             sol::table& outCtxTbl);

[[nodiscard]] sol::object BuildDefaultScalarValue(sol::state& lua, const Registry* reg, const ContractField& f,
                                                  int arrayIdx = 0, MockSeedVariant variant = MockSeedVariant::Compact);
[[nodiscard]] sol::table BuildSeededArrayForPattern(sol::state& lua, const Registry* reg, const OxmlContract& contract,
                                                    const std::string& arrayPath, int count, int depth,
                                                    MockSeedVariant variant = MockSeedVariant::Compact);
void SeedFieldsIntoTableWithVariant(sol::state& lua, const Registry* reg, sol::table& targetCtx,
                                    const OxmlContract& contract, MockSeedVariant variant, bool overwriteExisting);

[[nodiscard]] sol::table ResolveEffectivePresetTable(const sol::table& rootTbl, const std::string& requestedPreset,
                                                     std::string& outPresetKey);
[[nodiscard]] sol::table BuildLiveDocSnapshot(sol::state& lua, const UIPreviewer& previewer, const sol::table& ctx);
void ApplyMockTableFields(UIRuntime& uiRuntime, const sol::table& tbl, sol::table& outOpts, sol::table& outRawCtrl,
                          const sol::table& rootFallback = sol::table{});
bool LoadSandboxedMockScript(UIRuntime& uiRuntime, const std::string& resolvedPath, const std::string& requestedPreset,
                             sol::table& outOpts, sol::table& outRawCtrl, sol::table& outRawRoot,
                             std::string& outPresetKey, bool& outHasController, std::string& outError);

bool TryWriteTextFile(const std::string& path, const std::string& contents, std::string& outError);
void RecordMockMtime(const std::string& mockPath, std::filesystem::file_time_type& outTime, bool& outHasTime);
[[nodiscard]] bool AreSameDiskPath(const std::string& a, const std::string& b);
void AppendCallbackCommentHeader(std::ostringstream& ss, const OxmlContract& contract);
void AppendJsonStringList(std::ostringstream& ss, const std::vector<std::string>& items);
void AppendJsonIssuesArray(std::ostringstream& ss, const std::vector<UILayoutIssue>& issues,
                           std::string_view indent = "  ");

}  // namespace octarine::ui_preview_detail

#endif  // !OCTARINE_SHIPPED
