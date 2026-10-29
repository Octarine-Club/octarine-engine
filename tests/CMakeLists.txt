# =============================================================================
# Octarine Engine Test Suite
# =============================================================================

enable_testing()

if (MSVC)
    add_compile_options(/W4 /permissive- /MP /EHsc /bigobj /wd4201 /wd5321)
endif ()

# -----------------------------------------------------------------------------
# Test Registration Helpers
# -----------------------------------------------------------------------------

# Generic test helper
function(octarine_add_test TARGET CTEST_NAME)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS;DEFINES;INCLUDES" ${ARGN})
    add_executable(${TARGET} ${ARG_SOURCES})
    if (ARG_DEFINES)
        target_compile_definitions(${TARGET} PRIVATE ${ARG_DEFINES})
    endif ()
    if (ARG_INCLUDES)
        target_include_directories(${TARGET} PRIVATE ${ARG_INCLUDES})
    endif ()
    if (ARG_LIBS)
        target_link_libraries(${TARGET} PRIVATE ${ARG_LIBS})
    endif ()
    add_test(NAME ${CTEST_NAME} COMMAND ${TARGET})
endfunction()

# Engine-spanning test helper (links octarine_engine)
function(octarine_add_engine_test TARGET CTEST_NAME SOURCE)
    octarine_add_test(${TARGET} ${CTEST_NAME}
            SOURCES ${SOURCE}
            LIBS octarine_engine
            ${ARGN}
    )
    target_include_directories(${TARGET} PRIVATE "${CMAKE_SOURCE_DIR}/tests")
endfunction()

# Core ECS test helper (links ECS Registry and Logger only)
function(octarine_add_core_test EXE_NAME CTEST_NAME SOURCE)
    add_executable(${EXE_NAME} ${SOURCE}
            "${CMAKE_SOURCE_DIR}/src/ECS/Registry.cpp"
            "${CMAKE_SOURCE_DIR}/src/General/Logger.cpp")
    target_compile_definitions(${EXE_NAME} PRIVATE GLM_FORCE_INTRINSICS)
    target_include_directories(${EXE_NAME} PRIVATE
            "${CMAKE_SOURCE_DIR}/src"
            "${CMAKE_SOURCE_DIR}/libs"
            "${CMAKE_SOURCE_DIR}/tests")
    target_link_libraries(${EXE_NAME} PRIVATE glm::glm spdlog::spdlog Threads::Threads octarine_lua)
    add_test(NAME ${CTEST_NAME} COMMAND ${EXE_NAME})
endfunction()

# -----------------------------------------------------------------------------
# Core ECS & Systems Tests
# -----------------------------------------------------------------------------

octarine_add_core_test(OctarineEcsTest EcsRegistryTest EcsRegistryTest.cpp)
octarine_add_core_test(OctarineEcsHierarchyTest EcsHierarchyTest EcsHierarchyTest.cpp)
octarine_add_core_test(OctarineSystemLogicTest SystemLogicTest SystemLogicTest.cpp)
octarine_add_core_test(OctarineCollisionResponseTest CollisionResponseTest CollisionResponseTest.cpp)
octarine_add_core_test(OctarineKDTreeCollisionSystemTest KDTreeCollisionSystemTest KDTreeCollisionSystemTest.cpp)

if (TARGET octarine_game_module AND EXISTS "${OCTARINE_GAME_MODULE_DIR}/tests/CMakeLists.txt")
    add_subdirectory("${OCTARINE_GAME_MODULE_DIR}/tests" GameModuleTests)
    get_directory_property(_game_module_test_targets DIRECTORY "${OCTARINE_GAME_MODULE_DIR}/tests" BUILDSYSTEM_TARGETS)
    if (_game_module_test_targets)
        add_custom_target(octarine_game_module_tests DEPENDS ${_game_module_test_targets})
    endif ()
endif ()
octarine_add_test(OctarineStateInspectorTest StateInspectorTest
        SOURCES StateInspectorTest.cpp
        INCLUDES "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/libs"
        LIBS octarine_core
)
octarine_add_test(OctarineGoldenPathIntegrationTest GoldenPathIntegrationTest
        SOURCES GoldenPathIntegrationTest.cpp
        INCLUDES "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/libs"
        LIBS octarine_core
)
octarine_add_test(OctarineSimulationFuzzTest SimulationFuzzTest
        SOURCES SimulationFuzzTest.cpp
        INCLUDES "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/libs"
        LIBS octarine_core
)
octarine_add_test(OctarineScenarioRunnerTest ScenarioRunnerTest
        SOURCES ScenarioRunnerTest.cpp
        INCLUDES "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/libs"
        LIBS octarine_core
)

octarine_add_test(OctarineEventBusTest EventBusTest
        SOURCES "${CMAKE_SOURCE_DIR}/src/General/Logger.cpp" EventBusTest.cpp
        INCLUDES "${CMAKE_SOURCE_DIR}/src"
        LIBS spdlog::spdlog
)

octarine_add_test(OctarineCryptoTest CryptoTest
        SOURCES CryptoTest.cpp
        INCLUDES "${CMAKE_SOURCE_DIR}/src"
        LIBS octarine_assets octarine_core ${LUA_LIBRARIES}
)

# -----------------------------------------------------------------------------
# Asset Pipeline & Storage Tests
# -----------------------------------------------------------------------------

octarine_add_engine_test(OctarineAssetPipelineTest AssetPipelineTest AssetPipelineTest.cpp
        DEFINES ASSET_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/assets"
)

octarine_add_engine_test(OctarineAssetStoreTest AssetStoreTest AssetStoreTest.cpp)

octarine_add_test(OctarineAssetRefcounterTest AssetRefcounterTest
        SOURCES AssetRefcounterTest.cpp
        INCLUDES "${CMAKE_SOURCE_DIR}/src"
)

# -----------------------------------------------------------------------------
# Lua & Scripting Tests
# -----------------------------------------------------------------------------

octarine_add_engine_test(OctarineLuaApiTest LuaApiSmokeTest LuaApiSmokeTest.cpp
        DEFINES
            LUA_API_SMOKE_OUTPUT="${CMAKE_SOURCE_DIR}/lua_api.smoke.lua"
            LUA_API_COMPONENTS_OUTPUT="${CMAKE_SOURCE_DIR}/components.json"
            LUA_API_MODULES_OUTPUT="${CMAKE_SOURCE_DIR}/modules.json"
)

octarine_add_engine_test(OctarineLuaBehaviorTest LuaBehaviorTest LuaBehaviorTest.cpp)

octarine_add_test(OctarineFileSandboxTest FileSandboxTest
        SOURCES FileSandboxTest.cpp
        LIBS octarine_lua
)

# -----------------------------------------------------------------------------
# Subsystems & Tooling Tests
# -----------------------------------------------------------------------------

octarine_add_test(OctarineProcessTest ProcessTest
        SOURCES "${CMAKE_SOURCE_DIR}/src/Process/Process.cpp" ProcessTest.cpp
        INCLUDES "${CMAKE_SOURCE_DIR}/src"
        LIBS Threads::Threads
)

octarine_add_test(OctarineProjectIniTest ProjectIniTest
        SOURCES "${CMAKE_SOURCE_DIR}/src/General/IniParser.cpp"
                "${CMAKE_SOURCE_DIR}/src/Project/ProjectIni.cpp"
                ProjectIniTest.cpp
        INCLUDES "${CMAKE_SOURCE_DIR}/src"
)

octarine_add_engine_test(OctarineUserSettingsTest UserSettingsTest UserSettingsTest.cpp)

octarine_add_test(OctarineFileWatcherTest FileWatcherTest
        SOURCES "${CMAKE_SOURCE_DIR}/src/General/FileWatcher.cpp"
                "${CMAKE_SOURCE_DIR}/src/General/Logger.cpp"
                FileWatcherTest.cpp
        INCLUDES "${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}"
        LIBS spdlog::spdlog
)

octarine_add_test(OctarineAdbTest AdbTest
        SOURCES "${CMAKE_SOURCE_DIR}/src/Deploy/Adb.cpp"
                "${CMAKE_SOURCE_DIR}/src/Process/Process.cpp"
                "${CMAKE_SOURCE_DIR}/src/General/Logger.cpp"
                AdbTest.cpp
        INCLUDES "${CMAKE_SOURCE_DIR}/src"
        LIBS spdlog::spdlog Threads::Threads
)

octarine_add_engine_test(OctarineGameBakeTest GameBakeTest GameBakeTest.cpp
        DEFINES BAKE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/bake-project"
)

octarine_add_engine_test(OctarineRenderQueueTest RenderQueueTest RenderQueueTest.cpp)
octarine_add_engine_test(OctarineUILayoutTest UILayoutTest UILayoutTest.cpp)

if (NOT OCTARINE_SHIPPED)
    set(_extra_dev_libs "")
    if (WIN32)
        set(_extra_dev_libs ws2_32)
    endif ()
    octarine_add_test(OctarineDevListenServerTest DevListenServerTest
            SOURCES "${CMAKE_SOURCE_DIR}/src/Dev/DevListenServer.cpp"
                    "${CMAKE_SOURCE_DIR}/src/General/Logger.cpp"
                    DevListenServerTest.cpp
            INCLUDES "${CMAKE_SOURCE_DIR}/src"
            LIBS spdlog::spdlog Threads::Threads ${_extra_dev_libs}
    )
endif ()

message(STATUS "Octarine: Tests ENABLED")
