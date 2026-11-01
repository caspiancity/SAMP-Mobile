#pragma once
#include <mutex>

struct lua_State;

// Lua state-ine eyni anda tek thread toxunsun (hook-lar basqa thread-den gelir)
extern std::recursive_mutex g_luaMutex;

namespace LuaBridge {
    void Register(lua_State* L);      // findSymbol, hook, java*, saveData...
    void LoadModules(lua_State* L);   // files/lua/*.lua
    void Shutdown(lua_State* L);      // hook-lari sondur, java handle-lari azad et
}
