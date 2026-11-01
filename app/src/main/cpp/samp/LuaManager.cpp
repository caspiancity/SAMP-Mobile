#include "main.h"
#include "gui/gui.h"
#include "net/netgame.h"
#include "java/jniutil.h"
#include "vendor/lua/lua.hpp"

extern UI* pUI;
extern CNetGame* pNetGame;
extern CJavaWrapper* pJavaWrapper;
extern char* g_pszStorage;
void FLog(const char* fmt, ...);

static lua_State* L = nullptr;

// ---- Lua-ya açılan funksiyalar ----
static int l_addChat(lua_State* L) {
    const char* s = luaL_checkstring(L, 1);
    if (pUI) pUI->chat()->addDebugMessage("%s", s);
    return 0;
}
static int l_yt(lua_State* L) {
    const char* s = luaL_optstring(L, 1, "");
    if (pJavaWrapper) pJavaWrapper->YtCommand(s);
    return 0;
}
static int l_sendCommand(lua_State* L) {
    const char* s = luaL_checkstring(L, 1);
    if (pNetGame) pNetGame->SendChatCommand(s);
    return 0;
}
static int l_sendChat(lua_State* L) {
    const char* s = luaL_checkstring(L, 1);
    if (pNetGame) pNetGame->SendChatMessage(s);
    return 0;
}

static void Report(const char* what) {
    const char* err = lua_tostring(L, -1);
    FLog("[Lua] %s: %s", what, err ? err : "?");
    if (pUI && err) pUI->chat()->addDebugMessage("{FF0000}[Lua]: %s", err);
    lua_pop(L, 1);
}

namespace LuaManager {

void Shutdown() {
    if (L) { lua_close(L); L = nullptr; }
}

bool Init() {
    Shutdown();
    L = luaL_newstate();

    luaL_requiref(L, "_G", luaopen_base, 1);       lua_pop(L, 1);
    luaL_requiref(L, "string", luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, "table", luaopen_table, 1);   lua_pop(L, 1);
    luaL_requiref(L, "math", luaopen_math, 1);     lua_pop(L, 1);

    lua_register(L, "addChat", l_addChat);
    lua_register(L, "yt", l_yt);
    lua_register(L, "sendCommand", l_sendCommand);
    lua_register(L, "sendChat", l_sendChat);

    char path[512];
    snprintf(path, sizeof(path), "%s/core.lua", g_pszStorage);
    FLog("[Lua] yuklenir: %s", path);

    if (luaL_dofile(L, path) != LUA_OK) { Report("core.lua"); return false; }
    FLog("[Lua] core.lua yuklendi");
    return true;
}

// true qaytarsa komanda Lua tərəfindən "udulub"
bool OnCommand(const std::string& cmd, const std::string& args) {
    if (!L) return false;
    lua_getglobal(L, "onCommand");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return false; }
    lua_pushstring(L, cmd.c_str());
    lua_pushstring(L, args.c_str());
    if (lua_pcall(L, 2, 1, 0) != LUA_OK) { Report("onCommand"); return false; }
    bool handled = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return handled;
}

}