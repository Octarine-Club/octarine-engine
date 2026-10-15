#pragma once

// Single entry point that registers every component's LuaBinding<T> with LuaComponentRegistry.
void RegisterAllLuaBindings(bool includeGameModule = true);
