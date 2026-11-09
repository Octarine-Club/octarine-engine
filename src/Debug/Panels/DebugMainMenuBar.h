#pragma once

class Registry;

namespace octarine::debug {

#ifdef OCTARINE_WITH_IMGUI
void DrawDebugMainMenuBar(Registry& registry, float deltaTime);
#endif

}  // namespace octarine::debug
