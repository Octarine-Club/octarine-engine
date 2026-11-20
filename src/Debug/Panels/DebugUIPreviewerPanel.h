#pragma once

class Registry;

namespace octarine::debug {

#if defined(OCTARINE_WITH_IMGUI) && !defined(OCTARINE_SHIPPED)
void DrawUIPreviewerPanel(Registry& registry);
#endif

}  // namespace octarine::debug
