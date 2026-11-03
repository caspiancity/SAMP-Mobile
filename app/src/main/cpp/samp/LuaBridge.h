#pragma once
#include <mutex>
#include <string>

struct lua_State;

// Lua state-ine eyni anda tek thread toxunsun (hook-lar basqa thread-den gelir).
// Hook stub-lari kilidi 100ms gozleyib ala bilmese Lua-ni atlayib orijinal funksiyani
// cagirir (deadlock-dan qorunmaq ucun) -> bax: LuaBridge::Guard (LuaBridge.inl)
extern std::recursive_mutex g_luaMutex;

namespace LuaBridge {
    void Register(lua_State* L);      // findSymbol, hook, java*, saveData...
    void LoadModules(lua_State* L);   // files/lua/*.lua
    void Shutdown(lua_State* L);      // hook-lari sondur, java handle-lari azad et

    // files/lua/*.lua modullarina hadise yonlendirmesi
    bool DispatchCommand(lua_State* L, const std::string& cmd, const std::string& args);
    void DispatchRender(lua_State* L);
}