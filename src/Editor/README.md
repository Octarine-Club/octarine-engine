# Octarine Engine — Editor Subsystem (Experimental)

> [!WARNING]
> **Status: Experimental / Work-in-Progress**
> The C++ editor environment (`OCTARINE_WITH_EDITOR`) is currently experimental and disabled by default across standard builds, CI, and CMake presets. It is kept out of the day-to-day development loop until it can be stabilized and redesigned.

## Current Recommended Development Workflow

For day-to-day game development and debugging:
1. Build using the **`player-debug`** CMake preset (`OCTARINE_WITH_EDITOR=OFF`, `OCTARINE_WITH_IMGUI=ON`).
2. Build debug overlays and interactive developer tooling using **Dear ImGui directly in Lua**:
   - Implement the `onDebugGUI(self, entity)` lifecycle callback on script components.
   - Use the extensive global `ImGui.*` API table exposed via `sol2_ImGui_Bindings`.
   - Access built-in performance overlays, entity counters, and collider visualizations.

## Editor Architecture

When enabled via `-DOCTARINE_WITH_EDITOR=ON` (or using the `editor-debug` / `editor-release` presets), the editor builds into the `octarine_editor` static library:

- **Dockspace & Chrome**: `EditorPanelHost.cpp` and panels under `Panels/` manage the ImGui docking hierarchy, scene view render target, and toolbars.
- **Inspectors**: `Inspectors/` provide entity and component inspection surfaces (`RegisterAllInspectors.cpp`).
- **Persistence**: `EditorPersistence.cpp` handles window arrangement, audio preferences, and recent project paths.
- **Tools**: `PlayerLauncher.cpp` (external player process runner), `ExportBuilder.cpp` (package export dialog), and `HotPusher.cpp` (network hot reload pusher).
- **Secrets**: `Secrets/` provides OS-native keystore access for signing credentials.

## Future Stabilization Roadmap

When the editor is revisited in the future, goals include:
- Decoupling editor tools from the core game execution pipeline.
- Establishing seamless bidirectional sync between scene editing and Lua script states.
- Providing robust crash isolation so game script errors never crash the editor.
