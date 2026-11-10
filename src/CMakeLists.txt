# Engine layer split. Each STATIC library represents one architectural layer; PUBLIC
# link deps enforce the dependency direction at link time. Headers under src/ are reachable
# from every consumer (octarine_core publishes the include dir) — layering is enforced via
# symbol resolution, not via per-dir include walls.
#
# Layer map:
#   octarine_core      General/, ECS/, Components/, EventBus/, Events/, Game/GameConfig,
#                      Project/  (POD/data + cross-cutting utilities)
#   octarine_assets    AssetManager/ (catalog, atlas bakers, audio normalizer, pak)
#   octarine_renderer  Renderer/
#   octarine_systems   Systems/ (gameplay, audio, input, tilemap systems)
#   octarine_lua       Lua/Bindings, Lua/Modules, Lua/HotReload, Lua/LuaApiManifest
#   octarine_engine    Game/Game + Engine/EngineBootstrap (top-level coordinator)
#
# OctarineEngine (the desktop exe / Android shared lib) is declared in the root CMakeLists.txt
# and links octarine_engine only — everything else flows through PUBLIC deps.

# -----------------------------------------------------------------------------
# octarine_core — POD/data foundation. No SDL_image, no SDL_mixer, no imgui.
# -----------------------------------------------------------------------------
set(OCTARINE_CORE_SOURCES
        General/IniParser.cpp
        General/Logger.cpp
        General/FileSandbox.cpp
        General/FileWatcher.cpp
        General/LuaEscape.cpp
        General/PerformanceTracker.cpp
        General/SystemMemory.cpp
        Debug/DebugMenuRegistry.cpp
        Debug/SystemProfilerTracker.cpp
        ECS/Registry.cpp
        ECS/StateInspector.cpp
        Game/GameConfig.cpp
        Game/UserSettings.cpp
        Project/ProjectIni.cpp
        UI/OxmlParser.cpp
        UI/OssParser.cpp
)
if (UNIX AND NOT APPLE AND NOT ANDROID)
    list(APPEND OCTARINE_CORE_SOURCES General/SDL3_Linux_Stubs.cpp)
endif ()

octarine_library(octarine_core ${OCTARINE_CORE_SOURCES})
target_link_libraries(octarine_core PUBLIC
        glm::glm
        SDL3::SDL3
        sol2::sol2
        ${LUA_LIBRARIES}
        Threads::Threads
)
target_link_libraries(octarine_core PRIVATE spdlog::spdlog)

# Compile defines shared by every layer (consumers inherit via PUBLIC).
target_compile_definitions(octarine_core PUBLIC GLM_FORCE_INTRINSICS)
target_compile_definitions(octarine_core PUBLIC SOL_NO_CHECK_NUMBER_PRECISION=1)
set_source_files_properties(General/Logger.cpp PROPERTIES
        COMPILE_DEFINITIONS "OCTARINE_LOG_LEVEL_NAME=\"${_octarine_default_log_level}\"")

if (OCTARINE_ENABLE_PROFILING)
    target_compile_definitions(octarine_core PUBLIC OCTARINE_PROFILING)
endif ()
if (OCTARINE_WITH_IMGUI)
    target_compile_definitions(octarine_core PUBLIC OCTARINE_WITH_IMGUI)
endif ()
if (OCTARINE_SHIPPED)
    target_compile_definitions(octarine_core PUBLIC OCTARINE_SHIPPED)
endif ()
if (OCTARINE_PROTECT_SCRIPTS)
    target_compile_definitions(octarine_core PUBLIC OCTARINE_PROTECT_SCRIPTS)
endif ()
if (OCTARINE_PACKAGE_KEY)
    target_compile_definitions(octarine_core PUBLIC OCTARINE_PACKAGE_KEY="${OCTARINE_PACKAGE_KEY}")
endif ()
if (OCTARINE_ALLOW_LOOSE_ASSETS)
    target_compile_definitions(octarine_core PUBLIC OCTARINE_ALLOW_LOOSE_ASSETS)
endif ()

# -----------------------------------------------------------------------------
# octarine_assets — texture / font / audio handle store + catalog + bakers + pak.
# Per-file warning suppressions cover vendored stb single-header includes
# (TextureAtlasBaker, AtlasBaker, GlyphAtlas) and the BS.1770 size_t→double math
# in AudioNormalizer that trips the engine's strict -Wconversion baseline.
# -----------------------------------------------------------------------------
octarine_library(octarine_assets
        AssetManager/AssetCatalog.cpp
        AssetManager/AssetHotReload.cpp
        AssetManager/AssetManager.cpp
        AssetManager/AssetPak.cpp
        AssetManager/AtlasBaker.cpp
        AssetManager/AudioClipStore.cpp
        AssetManager/AudioNormalizer.cpp
        AssetManager/FontStore.cpp
        AssetManager/GlyphAtlas.cpp
        AssetManager/TextureAtlasBaker.cpp
        AssetManager/TextureStore.cpp
        AssetManager/SceneAssetScanner.cpp
)
set_source_files_properties(
        AssetManager/TextureAtlasBaker.cpp
        AssetManager/AtlasBaker.cpp
        AssetManager/GlyphAtlas.cpp
        AssetManager/AudioNormalizer.cpp
        PROPERTIES
            COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>"
            SKIP_PRECOMPILE_HEADERS ON)
target_link_libraries(octarine_assets PUBLIC
        octarine_core
        SDL3_ttf::SDL3_ttf
        $<IF:$<TARGET_EXISTS:SDL3_mixer::SDL3_mixer>,SDL3_mixer::SDL3_mixer,SDL3_mixer::SDL3_mixer-static>
        $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>
)
# libs/stb is its own include root so TextureAtlasBaker.cpp / AtlasBaker.cpp / GlyphAtlas.cpp can
# write `#include "stb_image.h"` (matches the pattern in scripts/octarine-icon-tool/).
target_include_directories(octarine_assets PRIVATE ${CMAKE_SOURCE_DIR}/libs/stb)
if (NOT MSVC)
    target_compile_options(octarine_assets PRIVATE $<$<NOT:$<CONFIG:Debug>>:-Os>)
endif ()

# -----------------------------------------------------------------------------
# octarine_renderer — render-queue producer drain + SDL_Renderer wrapper.
# -----------------------------------------------------------------------------
octarine_library(octarine_renderer
        Renderer/Renderer.cpp
        Renderer/RenderQueueStats.cpp
)
target_link_libraries(octarine_renderer PUBLIC
        octarine_core
        octarine_assets
)

# -----------------------------------------------------------------------------
# octarine_systems — per-frame Update systems + event-driven systems.
# -----------------------------------------------------------------------------
set(OCTARINE_SYSTEMS_SOURCES
        Systems/AudioSystem.cpp
        Systems/InputSystem.cpp
        Systems/TilemapSystem.cpp
        Tilemap/LdtkParser.cpp
        Tilemap/TilemapLoader.cpp
)
octarine_library(octarine_systems ${OCTARINE_SYSTEMS_SOURCES})
if (NOT MSVC)
    set_source_files_properties(Tilemap/LdtkParser.cpp PROPERTIES
            COMPILE_OPTIONS "$<$<NOT:$<CONFIG:Debug>>:-Os>"
            SKIP_PRECOMPILE_HEADERS ON)
endif ()
target_link_libraries(octarine_systems PUBLIC
        octarine_core
        octarine_assets
        octarine_renderer
)
if (OCTARINE_WITH_IMGUI)
    target_link_libraries(octarine_systems PUBLIC imgui::imgui)
endif ()
target_link_libraries(octarine_systems PRIVATE nlohmann_json::nlohmann_json)

# -----------------------------------------------------------------------------
# octarine_lua — sol2 component/module bindings + EmmyLua API manifest + hot reload.
# -----------------------------------------------------------------------------
octarine_library(octarine_lua
        Lua/LuaApiManifest.cpp
        Lua/LuaTableSerializer.cpp
        Lua/Bindings/InputSystemLuaBinding.cpp
        Lua/Bindings/RegisterAllBindings.cpp
        Lua/Bindings/RegisterTransformCoreBindings.cpp
        Lua/Bindings/RegisterRenderBindings.cpp
        Lua/Bindings/RegisterPhysicsAudioScriptBindings.cpp
        Lua/Bindings/RegisterUIBindings.cpp
        Lua/HotReload/ScriptHotReload.cpp
        Lua/Modules/AssetsModuleLuaBinding.cpp
        Lua/Modules/AudioModuleLuaBinding.cpp
        Lua/Modules/DevModuleLuaBinding.cpp
        Lua/Modules/EntityModuleLuaBinding.cpp
        Lua/Modules/GameModuleLuaBinding.cpp
        Lua/Modules/IoModuleLuaBinding.cpp
        Lua/Modules/LogModuleLuaBinding.cpp
        Lua/Modules/RegisterAllModules.cpp
        Lua/Modules/SceneModuleLuaBinding.cpp
        Lua/Modules/StorageModuleLuaBinding.cpp
        Lua/Modules/TilemapModuleLuaBinding.cpp
        Lua/Modules/UIModuleLuaBinding.cpp
        UI/UIRuntime.cpp
)
target_link_libraries(octarine_lua PUBLIC
        octarine_core
        octarine_assets
        octarine_renderer
        octarine_systems
)
if (OCTARINE_WITH_IMGUI)
    target_link_libraries(octarine_lua PUBLIC imgui::imgui sol2_ImGui_Bindings::sol2_ImGui_Bindings ${LUA_LIBRARIES})
endif ()
if (NOT MSVC)
    target_compile_options(octarine_lua PRIVATE $<$<NOT:$<CONFIG:Debug>>:-Os>)
endif ()

# -----------------------------------------------------------------------------
# Optional private GameModule (native game systems and components)
# -----------------------------------------------------------------------------
set(OCTARINE_GAME_MODULE_DIR "" CACHE PATH "Optional path to private game C++ module")
if (OCTARINE_GAME_MODULE_DIR AND EXISTS "${OCTARINE_GAME_MODULE_DIR}/CMakeLists.txt")
    message(STATUS "Octarine: Private GameModule FOUND at ${OCTARINE_GAME_MODULE_DIR}")
    add_subdirectory("${OCTARINE_GAME_MODULE_DIR}" GameModule)
endif ()


# -----------------------------------------------------------------------------
# octarine_engine — top-level coordinator. Owns Game/Game.cpp + EngineBootstrap, pulls every
# downstream lib in transitively.
# -----------------------------------------------------------------------------
set(OCTARINE_ENGINE_SOURCES
        Engine/EngineBootstrap.cpp
        Engine/SimulationSystems.cpp
        Engine/EngineRuntime.cpp
        Engine/FrameLoop.cpp
        Engine/Platform/PlatformPaths.cpp
        Engine/SceneLoader.cpp
        Game/Game.cpp
        Game/GameBake.cpp
)
if (NOT TARGET octarine_game_module)
    list(APPEND OCTARINE_ENGINE_SOURCES Engine/GameModuleNoop.cpp)
endif ()
# DevListenServer: TCP listener for the dev iterate loop. Available in dev player
# presets; compile-stripped from ship-release (OCTARINE_SHIPPED).
if (NOT OCTARINE_SHIPPED)
    list(APPEND OCTARINE_ENGINE_SOURCES Dev/DevListenServer.cpp Dev/DevListenDispatch.cpp)
endif ()
if (OCTARINE_WITH_IMGUI)
    list(APPEND OCTARINE_ENGINE_SOURCES
            Debug/DebugComponentInspectors.cpp
            Debug/DebugTheme.cpp
            Debug/Panels/DebugAssetScenePanel.cpp
            Debug/Panels/DebugEngineOptionsPanel.cpp
            Debug/Panels/DebugEntityInspectorPanel.cpp
            Debug/Panels/DebugLuaConsolePanel.cpp
            Debug/Panels/DebugMainMenuBar.cpp
            Debug/Panels/DebugSystemProfilerPanel.cpp
            Debug/Panels/DebugUIPanel.cpp
            Debug/Panels/DebugViewportGizmos.cpp
            Systems/RenderDebugGUISystem.cpp
            Systems/TilemapDebugPanel.cpp
    )
endif ()
octarine_library(octarine_engine ${OCTARINE_ENGINE_SOURCES})
target_link_libraries(octarine_engine PUBLIC
        octarine_core
        octarine_assets
        octarine_renderer
        octarine_lua
        octarine_systems
)
if (OCTARINE_WITH_IMGUI)
    target_link_libraries(octarine_engine PUBLIC imgui::imgui sol2_ImGui_Bindings::sol2_ImGui_Bindings ${LUA_LIBRARIES})
endif ()
if (TARGET octarine_game_module)
    target_link_libraries(octarine_engine PUBLIC octarine_game_module)
endif ()

# Winsock provides the BSD-socket API (ws2_32) on Windows; POSIX sockets come from libc so no
# extra link is needed there. Only when the dev server is compiled in (non-shipped builds).
if (NOT OCTARINE_SHIPPED AND WIN32)
    target_link_libraries(octarine_engine PUBLIC ws2_32)
endif ()
