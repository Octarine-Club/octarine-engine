#pragma once

class Registry;

namespace octarine::debug {

#ifdef OCTARINE_WITH_IMGUI
void DrawEngineOptionsPanel(Registry& registry);
#endif

}  // namespace octarine::debug
