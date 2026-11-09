#pragma once

class Registry;

namespace octarine::debug {

#ifdef OCTARINE_WITH_IMGUI
void DrawSystemProfilerPanel(Registry& registry);
#endif

}  // namespace octarine::debug
