#include "Debug/Panels/DebugLuaConsolePanel.h"

#ifdef OCTARINE_WITH_IMGUI
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <sol/sol.hpp>
#include <string>
#include <vector>

#include "ECS/Registry.h"
#include "General/Logger.h"
#include "imgui.h"

namespace octarine::debug {

namespace {

enum class SeverityFilter : std::uint8_t { All, Info, Warn, Error, Lua };

struct ConsoleState {
  char inputBuf[512] = "";
  std::vector<std::string> commandHistory;
  int historyPos = -1;
  bool autoScroll = true;
  bool scrollToBottom = false;
  SeverityFilter filter = SeverityFilter::All;
  ImGuiTextFilter textFilter;
  int logLevelIdx = 1;  // default debug
};

ConsoleState& GetConsoleState() {
  static ConsoleState state;
  return state;
}

void NavigateHistoryUp(ConsoleState* state) {
  if (state->historyPos == -1) {
    state->historyPos = static_cast<int>(state->commandHistory.size()) - 1;
  } else if (state->historyPos > 0) {
    state->historyPos--;
  }
}

void NavigateHistoryDown(ConsoleState* state) {
  if (state->historyPos != -1) {
    state->historyPos++;
    if (state->historyPos >= static_cast<int>(state->commandHistory.size())) {
      state->historyPos = -1;
    }
  }
}

void HandleHistoryNavigation(ConsoleState* state, ImGuiInputTextCallbackData* data) {
  const int prevPos = state->historyPos;
  if (data->EventKey == ImGuiKey_UpArrow) {
    NavigateHistoryUp(state);
  } else if (data->EventKey == ImGuiKey_DownArrow) {
    NavigateHistoryDown(state);
  }

  if (prevPos != state->historyPos) {
    const char* historyStr =
        (state->historyPos >= 0) ? state->commandHistory[static_cast<size_t>(state->historyPos)].c_str() : "";
    data->DeleteChars(0, data->BufTextLen);
    data->InsertChars(0, historyStr);
  }
}

int TextEditCallbackStub(ImGuiInputTextCallbackData* data) {
  auto* state = static_cast<ConsoleState*>(data->UserData);
  if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
    HandleHistoryNavigation(state, data);
  }
  return 0;
}

void ExecuteLuaCommand(Registry& registry, const std::string& command) {
  Logger::LogLua("> " + command);

  auto* luaPtr = registry.TryGet<sol::state*>();
  if (luaPtr == nullptr || *luaPtr == nullptr) {
    Logger::ErrorLua("Lua state is not available.");
    return;
  }
  sol::state& lua = **luaPtr;

  std::string code = command;
  if (code.starts_with("=")) {
    code = "return (" + code.substr(1) + ")";
  }

  sol::protected_function_result result = lua.safe_script("return (" + code + ")", sol::script_pass_on_error);
  if (!result.valid()) {
    result = lua.safe_script(code, sol::script_pass_on_error);
  }

  if (!result.valid()) {
    const auto err = result.get<sol::error>();
    Logger::ErrorLua(err.what());
    return;
  }

  if (result.return_count() > 0) {
    const sol::object obj = result.get<sol::object>(0);
    if (obj.is<std::string>()) {
      Logger::InfoLua(obj.as<std::string>());
    } else {
      sol::state_view sv(obj.lua_state());
      sol::protected_function_result strRes = sv["tostring"](obj);
      Logger::InfoLua(strRes.valid() && strRes.get_type() == sol::type::string ? strRes.get<std::string>()
                                                                               : "<result>");
    }
  }
}

bool MatchesSeverity(const std::string& line, SeverityFilter filter) {
  if (filter == SeverityFilter::All) return true;
  if (filter == SeverityFilter::Error && line.find("[error]") != std::string::npos) return true;
  if (filter == SeverityFilter::Warn && line.find("[warning]") != std::string::npos) return true;
  if (filter == SeverityFilter::Info && line.find("[info]") != std::string::npos) return true;
  if (filter == SeverityFilter::Lua && line.find("[lua]") != std::string::npos) return true;
  return false;
}

ImVec4 GetLineColor(const std::string& line) {
  if (line.find("[error]") != std::string::npos) return {1.0f, 0.4f, 0.4f, 1.0f};
  if (line.find("[warning]") != std::string::npos) return {1.0f, 0.8f, 0.3f, 1.0f};
  if (line.find("[lua]") != std::string::npos) return {0.5f, 0.9f, 0.5f, 1.0f};
  if (line.starts_with("> ")) return {0.4f, 0.8f, 1.0f, 1.0f};
  return {0.85f, 0.85f, 0.85f, 1.0f};
}

void DrawToolbar(ConsoleState& state) {
  if (ImGui::Button("Clear")) {
    Logger::ClearHistory();
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy All")) {
    std::string all;
    Logger::ForEachHistory([&all](const std::string& line) {
      all += line;
      all += "\n";
    });
    ImGui::SetClipboardText(all.c_str());
  }

  ImGui::SameLine();
  ImGui::Checkbox("Auto-scroll", &state.autoScroll);

  ImGui::SameLine();
  static constexpr const char* kLogLevels[] = {"trace", "debug", "info", "warn", "error"};
  ImGui::SetNextItemWidth(90.0f);
  if (ImGui::Combo("Log Level", &state.logLevelIdx, kLogLevels, 5)) {
    Logger::SetLevel(kLogLevels[state.logLevelIdx]);
  }

  ImGui::SameLine();
  state.textFilter.Draw("Search", 120.0f);

  ImGui::SameLine();
  if (ImGui::RadioButton("All", state.filter == SeverityFilter::All)) state.filter = SeverityFilter::All;
  ImGui::SameLine();
  if (ImGui::RadioButton("Error", state.filter == SeverityFilter::Error)) state.filter = SeverityFilter::Error;
  ImGui::SameLine();
  if (ImGui::RadioButton("Warn", state.filter == SeverityFilter::Warn)) state.filter = SeverityFilter::Warn;
  ImGui::SameLine();
  if (ImGui::RadioButton("Info", state.filter == SeverityFilter::Info)) state.filter = SeverityFilter::Info;
  ImGui::SameLine();
  if (ImGui::RadioButton("Lua", state.filter == SeverityFilter::Lua)) state.filter = SeverityFilter::Lua;
}

void DrawLogStream(ConsoleState& state) {
  const float footerHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
  if (ImGui::BeginChild("##log_stream", ImVec2(0, -footerHeight), true, ImGuiWindowFlags_HorizontalScrollbar)) {
    Logger::ForEachHistory([&state](const std::string& line) {
      if (!MatchesSeverity(line, state.filter)) return;
      if (!state.textFilter.PassFilter(line.c_str())) return;

      ImGui::PushStyleColor(ImGuiCol_Text, GetLineColor(line));
      ImGui::TextUnformatted(line.c_str());
      ImGui::PopStyleColor();
    });

    if (state.scrollToBottom || (state.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())) {
      ImGui::SetScrollHereY(1.0f);
    }
    state.scrollToBottom = false;
  }
  ImGui::EndChild();
}

}  // namespace

void DrawLuaConsolePanel(Registry& registry) {
  auto& state = GetConsoleState();

  DrawToolbar(state);
  ImGui::Separator();
  DrawLogStream(state);
  ImGui::Separator();

  ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory;
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::InputTextWithHint("##console_input", "Lua expression or command (=expr to eval)...", state.inputBuf,
                               sizeof(state.inputBuf), inputFlags, &TextEditCallbackStub, &state)) {
    std::string cmd(state.inputBuf);
    if (!cmd.empty()) {
      state.commandHistory.push_back(cmd);
      state.historyPos = -1;
      ExecuteLuaCommand(registry, cmd);
      state.inputBuf[0] = '\0';
      state.scrollToBottom = true;
    }
    ImGui::SetKeyboardFocusHere(-1);
  }
}

}  // namespace octarine::debug

#endif  // OCTARINE_WITH_IMGUI
