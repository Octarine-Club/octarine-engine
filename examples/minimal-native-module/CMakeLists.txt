# Minimal standalone native C++ game module example for Octarine Engine (ENG-DX-08).
# Build from the engine root with:
#   cmake --preset player-debug -DOCTARINE_ENABLE_TESTS=ON \
#     -DOCTARINE_GAME_MODULE_DIR=/path/to/octarine-engine/examples/minimal-native-module

set(GAME_MODULE_SOURCES
        GameModule.cpp
        Systems/HealthRegenSystem.cpp
)

octarine_library(octarine_game_module ${GAME_MODULE_SOURCES})

target_include_directories(octarine_game_module PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
)

target_link_libraries(octarine_game_module PUBLIC
        octarine::core
        octarine::assets
        octarine::renderer
        octarine::lua
        octarine::systems
)
