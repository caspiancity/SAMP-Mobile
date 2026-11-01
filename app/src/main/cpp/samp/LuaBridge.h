#pragma once
#include <mutex>
#include <string>

struct lua_State;

// Lua state-ine eyni anda tek thread toxunsun (hook-lar basqa thread-den gelir).
// recursive_timed_mutex: hook stub-lari 100ms gozleyib alinmasa Lua-ni atlayib
// orijinal funksiyani cagirir (deadlock-dan qorunmaq ucun).
extern std::recursive_timed_mutex g_luaMutex;

namespace LuaBridge {
    void Register(lua_State* L);      // findSymbol, hook, java*, saveData...
    void LoadModules(lua_State* L);   // files/lua/*.lua
    void Shutdown(lua_State* L);      // hook-lari sondur, java handle-lari azad et

    // files/lua/*.lua modullarina hadise yonlendirmesi
    bool DispatchCommand(lua_State* L, const std::string& cmd, const std::string& args);
    void DispatchRender(lua_State* L);
}
