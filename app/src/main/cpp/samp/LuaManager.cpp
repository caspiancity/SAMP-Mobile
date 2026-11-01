#include "main.h"
#include "LuaManager.h"
#include "gui/gui.h"
#include "net/netgame.h"
#include "game/game.h"
#include "game/VehControls.h"
#include "java/jniutil.h"
#include "vendor/lua/lua.hpp"
#include "imgui.h"
#include <time.h>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <sys/uio.h>

extern uintptr_t g_libGTASA;
extern uintptr_t g_libSAMP;
extern UI* pUI;
extern CGame* pGame;
extern CNetGame* pNetGame;
extern CJavaWrapper* pJavaWrapper;
extern char* g_pszStorage;
extern bool g_bHideAllUI;
extern bool g_bShowHandlingDlg;
void FLog(const char* fmt, ...);

static lua_State* L = nullptr;
static bool g_renderDisabled = false;
static float g_speedKmh = 0.0f;

// ======================= yardimci =======================
static double NowMs()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static CVehicle* GetLocalVehicle()
{
    CPlayerPed* ped = pGame ? pGame->FindPlayerPed() : nullptr;
    if (!ped || !ped->m_pPed || !ped->IsInVehicle()) return nullptr;
    if (!pNetGame || !pNetGame->GetVehiclePool()) return nullptr;

    CVehiclePool* pool = pNetGame->GetVehiclePool();
    VEHICLEID id = pool->FindIDFromGtaPtr(ped->m_pPed->pVehicle);
    if (id == INVALID_VEHICLE_ID) return nullptr;
    return pool->GetAt(id);
}

static void UpdateSpeed()
{
    static bool has = false;
    static float lx = 0, ly = 0, lz = 0;
    static double lt = 0;

    CPlayerPed* ped = pGame ? pGame->FindPlayerPed() : nullptr;
    if (!ped || !ped->m_pPed) { has = false; g_speedKmh = 0; return; }

    RwMatrix m = ped->m_pPed->GetMatrix().ToRwMatrix();
    double now = NowMs();

    if (has && now - lt > 1.0)
    {
        float dx = m.pos.x - lx, dy = m.pos.y - ly, dz = m.pos.z - lz;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        float dt = (float)((now - lt) / 1000.0);
        if (dist < 50.0f)   // teleport / spawn sicramalarini say
        {
            float raw = dist / dt * 3.6f;
            g_speedKmh += (raw - g_speedKmh) * 0.3f;
        }
    }
    lx = m.pos.x; ly = m.pos.y; lz = m.pos.z; lt = now; has = true;
}

static ImU32 Col(lua_State* S, int i) { return (ImU32)(unsigned long long)luaL_checkinteger(S, i); }

// ======================= Lua funksiyalari: chat / shebeke =======================
static int l_addChat(lua_State* S) {
    const char* s = luaL_checkstring(S, 1);
    if (pUI) pUI->chat()->addDebugMessage("%s", s);
    return 0;
}
static int l_yt(lua_State* S) {
    const char* s = luaL_optstring(S, 1, "");
    if (pJavaWrapper) pJavaWrapper->YtCommand(s);
    return 0;
}
static int l_sendCommand(lua_State* S) {
    const char* s = luaL_checkstring(S, 1);
    if (pNetGame) pNetGame->SendChatCommand(s);
    return 0;
}
static int l_sendChat(lua_State* S) {
    const char* s = luaL_checkstring(S, 1);
    if (pNetGame) pNetGame->SendChatMessage(s);
    return 0;
}

// ======================= ekran / toxunus / zaman =======================
static int l_screenSize(lua_State* S) {
    ImVec2 sz = ImGui::GetIO().DisplaySize;
    lua_pushnumber(S, sz.x);
    lua_pushnumber(S, sz.y);
    return 2;
}
static int l_getTime(lua_State* S) {
    lua_pushnumber(S, NowMs());
    return 1;
}
static int l_touch(lua_State* S) {
    ImGuiIO& io = ImGui::GetIO();
    lua_pushboolean(S, io.MouseDown[0]);
    lua_pushnumber(S, io.MousePos.x);
    lua_pushnumber(S, io.MousePos.y);
    return 3;
}

// ======================= cizim =======================
static int l_drawRect(lua_State* S) {
    float x = (float)luaL_checknumber(S, 1), y = (float)luaL_checknumber(S, 2);
    float w = (float)luaL_checknumber(S, 3), h = (float)luaL_checknumber(S, 4);
    ImU32 c = Col(S, 5);
    float r = (float)luaL_optnumber(S, 6, 0);
    bool filled = lua_isnone(S, 7) ? true : lua_toboolean(S, 7) != 0;
    float th = (float)luaL_optnumber(S, 8, 1);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (filled) dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), c, r);
    else        dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), c, r, ~0, th);
    return 0;
}
static int l_drawLine(lua_State* S) {
    ImGui::GetForegroundDrawList()->AddLine(
        ImVec2((float)luaL_checknumber(S, 1), (float)luaL_checknumber(S, 2)),
        ImVec2((float)luaL_checknumber(S, 3), (float)luaL_checknumber(S, 4)),
        Col(S, 5), (float)luaL_optnumber(S, 6, 1));
    return 0;
}
static int l_drawCircle(lua_State* S) {
    ImVec2 c((float)luaL_checknumber(S, 1), (float)luaL_checknumber(S, 2));
    float r = (float)luaL_checknumber(S, 3);
    ImU32 col = Col(S, 4);
    bool filled = lua_isnone(S, 5) ? true : lua_toboolean(S, 5) != 0;
    float th = (float)luaL_optnumber(S, 6, 1);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (filled) dl->AddCircleFilled(c, r, col, 40);
    else        dl->AddCircle(c, r, col, 40, th);
    return 0;
}
// drawArc(cx, cy, r, a0, a1, color, thickness)  -- radian
static int l_drawArc(lua_State* S) {
    float cx = (float)luaL_checknumber(S, 1), cy = (float)luaL_checknumber(S, 2);
    float r = (float)luaL_checknumber(S, 3);
    float a0 = (float)luaL_checknumber(S, 4), a1 = (float)luaL_checknumber(S, 5);
    ImU32 c = Col(S, 6);
    float th = (float)luaL_optnumber(S, 7, 1);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const int n = 48;
    ImVec2 prev(cx + cosf(a0) * r, cy + sinf(a0) * r);
    for (int i = 1; i <= n; i++) {
        float a = a0 + (a1 - a0) * (float)i / (float)n;
        ImVec2 cur(cx + cosf(a) * r, cy + sinf(a) * r);
        dl->AddLine(prev, cur, c, th);
        prev = cur;
    }
    return 0;
}
static int l_drawText(lua_State* S) {
    float x = (float)luaL_checknumber(S, 1), y = (float)luaL_checknumber(S, 2);
    const char* t = luaL_checkstring(S, 3);
    ImU32 c = Col(S, 4);
    float size = (float)luaL_optnumber(S, 5, 18);
    ImGui::GetForegroundDrawList()->AddText(ImGui::GetFont(), size, ImVec2(x, y), c, t);
    return 0;
}
static int l_textSize(lua_State* S) {
    const char* t = luaL_checkstring(S, 1);
    float size = (float)luaL_optnumber(S, 2, 18);
    ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, t);
    lua_pushnumber(S, sz.x);
    lua_pushnumber(S, sz.y);
    return 2;
}

// ======================= oyun / masin =======================
static int l_inVehicle(lua_State* S) {
    CPlayerPed* ped = pGame ? pGame->FindPlayerPed() : nullptr;
    lua_pushboolean(S, ped && ped->IsInVehicle());
    return 1;
}
static int l_getSpeed(lua_State* S) {
    lua_pushnumber(S, g_speedKmh);
    return 1;
}
static int l_getPlayerPos(lua_State* S) {
    CPlayerPed* ped = pGame ? pGame->FindPlayerPed() : nullptr;
    if (!ped || !ped->m_pPed) return 0;
    RwMatrix m = ped->m_pPed->GetMatrix().ToRwMatrix();
    lua_pushnumber(S, m.pos.x);
    lua_pushnumber(S, m.pos.y);
    lua_pushnumber(S, m.pos.z);
    return 3;
}
// setSignal(0=off, 1=sol, 2=sag, 3=avariya)
static int l_setSignal(lua_State* S) {
    int m = (int)luaL_checkinteger(S, 1);
    CVehicle* v = GetLocalVehicle();
    if (!v || !v->m_pVehicle) return 0;
    uint8_t mode = VEH_SIGNAL_OFF;
    if (m == 1) mode = VEH_SIGNAL_LEFT;
    else if (m == 2) mode = VEH_SIGNAL_RIGHT;
    else if (m == 3) mode = VEH_SIGNAL_HAZARD;
    VehControls::SetSignal(v->m_pVehicle, mode);
    return 0;
}
static int l_setHeadlight(lua_State* S) {      // palitra indeksi 0-15
    CVehicle* v = GetLocalVehicle();
    if (v) v->SetHeadlightColor((uint8_t)luaL_checkinteger(S, 1));
    return 0;
}
static int l_setMaxSpeed(lua_State* S) {
    CVehicle* v = GetLocalVehicle();
    if (v) v->SetMaxSpeed((float)luaL_checknumber(S, 1));
    return 0;
}
static int l_setSteering(lua_State* S) {
    CVehicle* v = GetLocalVehicle();
    if (v) v->SetSteeringAngle((float)luaL_checknumber(S, 1));
    return 0;
}
static int l_setBrake(lua_State* S) {
    CVehicle* v = GetLocalVehicle();
    if (v) v->SetBrakePower((float)luaL_checknumber(S, 1));
    return 0;
}
static int l_setHideUI(lua_State* S) {
    g_bHideAllUI = lua_toboolean(S, 1) != 0;
    return 0;
}
static int l_toggleHandlingDlg(lua_State* S) {
    g_bShowHandlingDlg = !g_bShowHandlingDlg;
    return 0;
}

// ======================= UMUMI YADDAS ALET QUTUSU =======================
// Bir defe yazilir. Bundan sonra struktur sahelerini (teker, suspenziya, kamera,
// qapi, motor, far...) Lua-dan ofsetle oxuyub yazirsan. C++ build lazim deyil.
// process_vm_readv/writev istifade olunur: sehv adres crash vermir, sadece false qayidir.

static bool MemRead(uintptr_t addr, void* out, size_t n) {
    if (addr < 0x10000) return false;
    iovec l{ out, n };
    iovec r{ (void*)addr, n };
    return process_vm_readv(getpid(), &l, 1, &r, 1, 0) == (ssize_t)n;
}
static bool MemWrite(uintptr_t addr, const void* in, size_t n) {
    if (addr < 0x10000) return false;
    iovec l{ (void*)in, n };
    iovec r{ (void*)addr, n };
    return process_vm_writev(getpid(), &l, 1, &r, 1, 0) == (ssize_t)n;
}

// baza adresleri ve obyekt gostericileri
static int l_gtaBase(lua_State* S)  { lua_pushinteger(S, (lua_Integer)g_libGTASA); return 1; }
static int l_sampBase(lua_State* S) { lua_pushinteger(S, (lua_Integer)g_libSAMP);  return 1; }
static int l_vehPtr(lua_State* S) {
    CVehicle* v = GetLocalVehicle();
    lua_pushinteger(S, (v && v->m_pVehicle) ? (lua_Integer)(uintptr_t)v->m_pVehicle : 0);
    return 1;
}
static int l_pedPtr(lua_State* S) {
    CPlayerPed* p = pGame ? pGame->FindPlayerPed() : nullptr;
    lua_pushinteger(S, (p && p->m_pPed) ? (lua_Integer)(uintptr_t)p->m_pPed : 0);
    return 1;
}
static int l_camPtr(lua_State* S) {
    lua_pushinteger(S, (lua_Integer)(g_libGTASA + (VER_x32 ? 0x00951FA8 : 0xBBA8D0)));
    return 1;
}

// readMem(addr, "u8"|"u16"|"u32"|"i32"|"u64"|"f32"|"ptr") -> deyer ve ya nil
static int l_readMem(lua_State* S) {
    uintptr_t a = (uintptr_t)luaL_checkinteger(S, 1);
    const char* t = luaL_checkstring(S, 2);
    if (!strcmp(t, "u8"))       { uint8_t  v; if (!MemRead(a, &v, 1)) return 0; lua_pushinteger(S, v); }
    else if (!strcmp(t, "u16")) { uint16_t v; if (!MemRead(a, &v, 2)) return 0; lua_pushinteger(S, v); }
    else if (!strcmp(t, "u32")) { uint32_t v; if (!MemRead(a, &v, 4)) return 0; lua_pushinteger(S, v); }
    else if (!strcmp(t, "i32")) { int32_t  v; if (!MemRead(a, &v, 4)) return 0; lua_pushinteger(S, v); }
    else if (!strcmp(t, "u64") || !strcmp(t, "ptr")) { uint64_t v; if (!MemRead(a, &v, 8)) return 0; lua_pushinteger(S, (lua_Integer)v); }
    else if (!strcmp(t, "f32")) { float    v; if (!MemRead(a, &v, 4)) return 0; lua_pushnumber(S, v); }
    else return luaL_error(S, "readMem: bilinmeyen tip '%s'", t);
    return 1;
}

// writeMem(addr, tip, deyer) -> true/false
static int l_writeMem(lua_State* S) {
    uintptr_t a = (uintptr_t)luaL_checkinteger(S, 1);
    const char* t = luaL_checkstring(S, 2);
    bool ok = false;
    if (!strcmp(t, "u8"))       { uint8_t  v = (uint8_t)luaL_checkinteger(S, 3);  ok = MemWrite(a, &v, 1); }
    else if (!strcmp(t, "u16")) { uint16_t v = (uint16_t)luaL_checkinteger(S, 3); ok = MemWrite(a, &v, 2); }
    else if (!strcmp(t, "u32") || !strcmp(t, "i32")) { uint32_t v = (uint32_t)luaL_checkinteger(S, 3); ok = MemWrite(a, &v, 4); }
    else if (!strcmp(t, "u64") || !strcmp(t, "ptr")) { uint64_t v = (uint64_t)luaL_checkinteger(S, 3); ok = MemWrite(a, &v, 8); }
    else if (!strcmp(t, "f32")) { float    v = (float)luaL_checknumber(S, 3);     ok = MemWrite(a, &v, 4); }
    else return luaL_error(S, "writeMem: bilinmeyen tip '%s'", t);
    lua_pushboolean(S, ok);
    return 1;
}

// callFunc(adres, a1..a6)  -- tam eded/gostericili arqumentler. Qayidis: tam eded.
// Diqqet: sehv adres/arqument oyunu crash edir. this-gosterici ilk arqumentdir.
// Float arqument gotuern funksiyalar ucun deyil.
static int l_callFunc(lua_State* S) {
    uintptr_t fn = (uintptr_t)luaL_checkinteger(S, 1);
    if (fn < 0x10000) return luaL_error(S, "callFunc: yanlis adres");
    uintptr_t a[6] = { 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 6; i++) a[i] = (uintptr_t)luaL_optinteger(S, 2 + i, 0);
    typedef uintptr_t (*Fn6)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    uintptr_t r = ((Fn6)fn)(a[0], a[1], a[2], a[3], a[4], a[5]);
    lua_pushinteger(S, (lua_Integer)r);
    return 1;
}

// ======================= idare =======================
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
    g_renderDisabled = false;
    L = luaL_newstate();

    luaL_requiref(L, "_G", luaopen_base, 1);       lua_pop(L, 1);
    luaL_requiref(L, "string", luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, "table", luaopen_table, 1);   lua_pop(L, 1);
    luaL_requiref(L, "math", luaopen_math, 1);     lua_pop(L, 1);

    lua_register(L, "addChat", l_addChat);
    lua_register(L, "yt", l_yt);
    lua_register(L, "sendCommand", l_sendCommand);
    lua_register(L, "sendChat", l_sendChat);

    lua_register(L, "screenSize", l_screenSize);
    lua_register(L, "getTime", l_getTime);
    lua_register(L, "touch", l_touch);

    lua_register(L, "drawRect", l_drawRect);
    lua_register(L, "drawLine", l_drawLine);
    lua_register(L, "drawCircle", l_drawCircle);
    lua_register(L, "drawArc", l_drawArc);
    lua_register(L, "drawText", l_drawText);
    lua_register(L, "textSize", l_textSize);

    lua_register(L, "inVehicle", l_inVehicle);
    lua_register(L, "getSpeed", l_getSpeed);
    lua_register(L, "getPlayerPos", l_getPlayerPos);
    lua_register(L, "setSignal", l_setSignal);
    lua_register(L, "setHeadlight", l_setHeadlight);
    lua_register(L, "setMaxSpeed", l_setMaxSpeed);
    lua_register(L, "setSteering", l_setSteering);
    lua_register(L, "setBrake", l_setBrake);
    lua_register(L, "setHideUI", l_setHideUI);
    lua_register(L, "toggleHandlingDlg", l_toggleHandlingDlg);

    lua_register(L, "gtaBase", l_gtaBase);
    lua_register(L, "sampBase", l_sampBase);
    lua_register(L, "vehPtr", l_vehPtr);
    lua_register(L, "pedPtr", l_pedPtr);
    lua_register(L, "camPtr", l_camPtr);
    lua_register(L, "readMem", l_readMem);
    lua_register(L, "writeMem", l_writeMem);
    lua_register(L, "callFunc", l_callFunc);

    char path[512];
    snprintf(path, sizeof(path), "%s/core.lua", g_pszStorage);
    FLog("[Lua] yuklenir: %s", path);

    if (luaL_dofile(L, path) != LUA_OK) { Report("core.lua"); return false; }
    FLog("[Lua] core.lua yuklendi");
    return true;
}

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

void OnRender() {
    if (!L || g_renderDisabled) return;
    if (!ImGui::GetCurrentContext()) return;

    UpdateSpeed();

    lua_getglobal(L, "onRender");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        Report("onRender");
        g_renderDisabled = true;   // xeta chati doldurmasin; /luareload ile yeniden acilir
    }
}

}
