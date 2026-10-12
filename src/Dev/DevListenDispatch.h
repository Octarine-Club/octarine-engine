#pragma once

#ifndef OCTARINE_SHIPPED

#include "Dev/DevListenServer.h"

class Registry;
class Game;
namespace sol {
class state;
}

namespace octarine::dev {

// Builds the main-thread CommandHandler that applies the dev-listen engine-mutating ops (eval_lua,
// reload_scene, push_script, push_asset, step, pause, inspect) against the live engine. Captures non-owning
// references to the Registry + sol::state and optional Game pointer. Install the result via
// DevListenServer::SetCommandHandler. Lives in the engine layer so the transport (DevListenServer)
// stays free of Registry / sol / AssetManager dependencies and its smoke test can build standalone.
CommandHandler MakeEngineCommandHandler(Registry& registry, sol::state& lua, Game* game = nullptr);

}  // namespace octarine::dev

#endif  // !OCTARINE_SHIPPED
