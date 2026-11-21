#!/usr/bin/env python3
import re, shutil, sys
R = "app/src/main/cpp/samp/"

def rd(p): return open(R + p, encoding="utf-8", errors="surrogateescape", newline="").read()
def wr(p, s): open(R + p, "w", encoding="utf-8", errors="surrogateescape", newline="").write(s)
def one(s, pat, fn, name, flags=re.M):
    r, n = re.subn(pat, fn, s, flags=flags)
    if n != 1:
        print("XƏTA: '%s' tapılmadı/çox tapıldı (n=%d) -> dayandım, heç nə yazılmadı" % (name, n)); sys.exit(1)
    return r

CAMX = r'''
// ============================================================================
// AZ_CAMX : kamera sistemi (kamera ikonu ile deyisme, pinch zoom, MTA, FPS, dron, FOV)
// ============================================================================
int LineOfSight(RwV3d* start, RwV3d* end, void* colpoint, uintptr_t ent, char buildings, char vehicles, char peds, char objects, char dummies, bool seeThrough, bool camera, bool unk);
float g_camxFovOffset = 0.0f;

namespace CamX
{
    enum { M_GAME = 0, M_CHASE = 1, M_MTA = 2, M_FPS = 3 };
    enum { MAXP = 8 };
    static int   s_mode = M_GAME, s_prevMode = M_CHASE;
    static bool  s_active = false, s_drone = false, s_pinching = false;
    static float s_yaw = 0.0f, s_pitch = -0.22f, s_dist = 5.0f;
    static float s_distChase = 5.0f, s_distMta = 7.5f, s_lastH = 0.0f, s_idle = 0.0f;
    static float s_sens = 0.0045f, s_dspeed = 22.0f;
    static CVector s_dpos;
    static bool  p_down[MAXP] = {}, p_rz[MAXP] = {};
    static float p_x[MAXP] = {}, p_y[MAXP] = {};
    static int   s_look = -1, s_stick = -1;
    static float s_lx = 0, s_ly = 0, s_sx = 0, s_sy = 0, s_mx = 0, s_my = 0;
    static float s_pinchPrev = 0.0f, s_pinchAcc = 0.0f;

    static float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static float WrapPi(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
    static bool  Own() { return s_mode != M_GAME || s_drone; }
    static CCamera& Cam() { return *reinterpret_cast<CCamera*>(g_libGTASA + (VER_x32 ? 0x00951FA8 : 0xBBA8D0)); }

    static void ClampPitch()
    {
        const float lo = (s_mode == M_FPS || s_drone) ? -1.45f : -1.25f;
        const float hi = (s_mode == M_FPS || s_drone) ?  1.45f :  0.12f;   // 3-cu shexsde kamera yerin altina dusmesin
        s_pitch = Clampf(s_pitch, lo, hi);
    }

    static CPedGTA* Ped()
    {
        CPlayerPed* p = pGame ? pGame->FindPlayerPed() : nullptr;
        return (p && p->m_pPed) ? p->m_pPed : nullptr;
    }

    static bool GetTarget(CVector& t, float& h, bool& inVeh)
    {
        CPedGTA* g = Ped();
        if (!g) return false;
        inVeh = g->IsInVehicle();
        if (inVeh) {
            CVehicleGTA* v = g->pVehicle;
            RwMatrix m = v->GetMatrix().ToRwMatrix();
            h = atan2f(m.up.y, m.up.x);
            const CVector p = v->GetPosition();
            t.x = p.x; t.y = p.y; t.z = p.z + 0.9f;
        } else {
            RwMatrix m = g->GetMatrix().ToRwMatrix();
            h = atan2f(m.up.y, m.up.x);
            const CVector p = g->GetPosition();
            t.x = p.x; t.y = p.y; t.z = p.z + 0.75f;
        }
        return true;
    }

    // oyunun hazirki kamerasindan yaw/pitch/mesafe goturur (keçid hhamar olsun)
    static void InitFromGameCam(float defDist)
    {
        CVector t; float h = 0.0f; bool v = false;
        if (!GetTarget(t, h, v)) return;
        const CVector c = Cam().GetPosition();
        const float dx = t.x - c.x, dy = t.y - c.y, dz = t.z - c.z;
        const float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d < 1.0f || d > 40.0f) { s_yaw = h; s_pitch = -0.22f; s_dist = defDist; }
        else { s_yaw = atan2f(dy, dx); s_pitch = asinf(Clampf(dz / d, -1.0f, 1.0f)); s_dist = d; }
        ClampPitch();
    }

    static void SetMode(int m)
    {
        if (m == s_mode) return;
        if (s_mode == M_CHASE) s_distChase = s_dist; else if (s_mode == M_MTA) s_distMta = s_dist;
        const int old = s_mode;
        if (old == M_GAME) InitFromGameCam(5.0f);
        if (m == M_CHASE) { if (old == M_GAME) s_distChase = s_dist; s_dist = Clampf(s_distChase, 1.6f, 16.0f); }
        else if (m == M_MTA) s_dist = Clampf(s_distMta, 2.5f, 25.0f);
        if (m == M_FPS) s_prevMode = (old == M_GAME) ? (int)M_CHASE : old;
        s_mode = m; s_idle = 0.0f; ClampPitch();
    }

    static void ApplyPinch(float delta)      // delta>0: barmaqlar aralanir = yaxinlasdir
    {
        if (s_drone) { s_dspeed = Clampf(s_dspeed + delta * 0.06f, 5.0f, 120.0f); return; }
        if (s_mode == M_GAME) {
            s_pinchAcc += delta;
            if (fabsf(s_pinchAcc) < 40.0f) return;
            SetMode(M_CHASE); s_pinchAcc = 0.0f;
        }
        if (s_mode == M_FPS) {
            if (delta < 0.0f) { s_pinchAcc += delta; if (s_pinchAcc < -70.0f) { SetMode(s_prevMode); s_pinchAcc = 0.0f; } }
            else s_pinchAcc = 0.0f;
            return;
        }
        s_dist -= delta * 0.02f * (1.0f + s_dist * 0.15f);
        if (s_dist < 1.3f) { s_dist = 2.0f; SetMode(M_FPS); s_pinchAcc = 0.0f; return; }
        s_dist = Clampf(s_dist, 1.3f, s_mode == M_MTA ? 25.0f : 16.0f);
    }

    static bool LosClear(const CVector& a, const CVector& b)
    {
        RwV3d s = { a.x, a.y, a.z }, e = { b.x, b.y, b.z };
        alignas(16) char col[256]; uintptr_t hitEnt = 0;
        return LineOfSight(&s, &e, col, (uintptr_t)&hitEnt, 1, 0, 0, 0, 0, false, true, false) == 0;
    }

    static void Place(const CVector& P, const CVector& T)
    {
        CCamera::SetPosition(P.x, P.y, P.z, 0.0f, 0.0f, 0.0f);
        CCamera::LookAtPoint(T.x, T.y, T.z, 2);
    }

    static void SetDrone(bool on)
    {
        if (on == s_drone) return;
        if (on) {
            if (s_mode == M_GAME) InitFromGameCam(5.0f);
            const CVector c = Cam().GetPosition();
            s_dpos.x = c.x; s_dpos.y = c.y; s_dpos.z = c.z;
            s_stick = -1; s_mx = s_my = 0.0f; s_look = -1;
            s_drone = true;
        } else { s_drone = false; s_stick = -1; s_mx = s_my = 0.0f; ClampPitch(); }
    }
}

bool CamX_OnTouch(int type, int num, int x, int y)
{
    using namespace CamX;
    if (num < 0 || num >= MAXP) return false;
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    const float fx = (float)x, fy = (float)y;
    bool consume = false;

    if (type == 2) {                                   // push
        p_down[num] = true; p_x[num] = fx; p_y[num] = fy;
        p_rz[num] = fx > d.x * 0.35f && fy < d.y * 0.72f;
        if (s_drone && !p_rz[num] && fy > d.y * 0.30f && s_stick < 0) { s_stick = num; s_sx = fx; s_sy = fy; s_mx = s_my = 0.0f; }
        if (num == s_stick) consume = true;
        else if (p_rz[num] && s_look < 0 && Own() && !s_pinching) { s_look = num; s_lx = fx; s_ly = fy; }
    }
    else if (type == 3) {                              // move
        p_x[num] = fx; p_y[num] = fy;
        if (num == s_stick) {
            const float Rr = d.y * 0.16f;
            s_mx = Clampf((fx - s_sx) / Rr, -1.0f, 1.0f); s_my = Clampf((fy - s_sy) / Rr, -1.0f, 1.0f);
            consume = true;
        } else if (num == s_look && !s_pinching) {
            s_yaw   -= (fx - s_lx) * s_sens;           // saga surusdur -> saga bax
            s_pitch -= (fy - s_ly) * s_sens;           // asagi surusdur -> asagi bax
            s_lx = fx; s_ly = fy; s_idle = 0.0f; ClampPitch();
        } else if (s_look < 0 && p_rz[num] && Own() && !s_pinching) { s_look = num; s_lx = fx; s_ly = fy; }
    }
    else if (type == 1) {                              // pop
        if (num == s_stick) { s_stick = -1; s_mx = s_my = 0.0f; consume = true; }
        if (num == s_look) s_look = -1;
        p_down[num] = false; p_rz[num] = false;
    }

    // pinch (zonada 2 barmaq)
    if (type != 1) {
        int a = -1, b = -1;
        for (int i = 0; i < MAXP; ++i) if (p_down[i] && p_rz[i]) { if (a < 0) a = i; else if (b < 0) b = i; }
        if (a >= 0 && b >= 0) {
            const float dd = hypotf(p_x[a] - p_x[b], p_y[a] - p_y[b]);
            if (!s_pinching) { s_pinching = true; s_pinchPrev = dd; s_pinchAcc = 0.0f; s_look = -1; }
            else if (type == 3 && (num == a || num == b)) { const float dl = dd - s_pinchPrev; s_pinchPrev = dd; ApplyPinch(dl); }
        } else s_pinching = false;
    } else {
        int cnt = 0;
        for (int i = 0; i < MAXP; ++i) if (p_down[i] && p_rz[i]) ++cnt;
        if (cnt < 2) s_pinching = false;
    }
    return consume;
}

void CamX_Update()
{
    using namespace CamX;
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last).count(); last = now;
    if (dt > 0.05f) dt = 0.05f;
    if (dt < 0.0f) dt = 0.0f;

    if (!Own()) {
        if (s_active) { s_active = false; s_look = -1; s_stick = -1; CCamera::SetBehindPlayer(); }
        return;
    }
    if (!pGame || !pNetGame || pGame->IsGamePaused()) return;

    CVector t; float h = 0.0f; bool inVeh = false;
    if (!GetTarget(t, h, inVeh)) return;
    if (!s_active) { s_active = true; s_lastH = h; }
    if (inVeh && s_mode == M_FPS && !s_drone) s_yaw += WrapPi(h - s_lastH);   // masin donende baxis da donsun
    s_lastH = h;
    if (s_look < 0) s_idle += dt;

    // CHASE: masinda barmaq yoxdursa kamera ozu arxaya qayidir (MTA-da hec vaxt)
    if (s_mode == M_CHASE && inVeh && !s_drone && s_look < 0 && s_idle > 1.2f) {
        const float k = Clampf(dt * 2.2f, 0.0f, 1.0f);
        s_yaw += WrapPi(h - s_yaw) * k;
        s_pitch += (-0.2f - s_pitch) * k * 0.7f;
    }

    const float cp = cosf(s_pitch), sp = sinf(s_pitch), cy = cosf(s_yaw), sy = sinf(s_yaw);
    CVector dir; dir.x = cp * cy; dir.y = cp * sy; dir.z = sp;

    if (s_drone) {
        const float fwd = -s_my, str = s_mx;
        s_dpos.x += (dir.x * fwd + sy * str) * s_dspeed * dt;
        s_dpos.y += (dir.y * fwd - cy * str) * s_dspeed * dt;
        s_dpos.z += (dir.z * fwd) * s_dspeed * dt;
        const float ex = s_dpos.x - t.x, ey = s_dpos.y - t.y, ez = s_dpos.z - t.z;
        const float dd = sqrtf(ex * ex + ey * ey + ez * ez);
        if (dd > 300.0f) { const float k = 300.0f / dd; s_dpos.x = t.x + ex * k; s_dpos.y = t.y + ey * k; s_dpos.z = t.z + ez * k; }
        CVector T; T.x = s_dpos.x + dir.x; T.y = s_dpos.y + dir.y; T.z = s_dpos.z + dir.z;
        Place(s_dpos, T);
        return;
    }

    if (s_mode == M_FPS) {
        CPedGTA* g = Ped();
        RwV3d bp = { 0.0f, 0.0f, 0.0f };
        if (g) g->GetBonePosition(&bp, 5, false);               // 5 = bash
        if (bp.x == 0.0f && bp.y == 0.0f && bp.z == 0.0f) { bp.x = t.x; bp.y = t.y; bp.z = t.z - 0.1f; }
        CVector P, T;
        P.x = bp.x + dir.x * 0.12f; P.y = bp.y + dir.y * 0.12f; P.z = bp.z + dir.z * 0.12f + 0.03f;
        T.x = P.x + dir.x * 5.0f; T.y = P.y + dir.y * 5.0f; T.z = P.z + dir.z * 5.0f;
        Place(P, T);
        return;
    }

    float dd = s_dist; CVector P;
    for (int i = 0; i < 6; ++i) {
        P.x = t.x - dir.x * dd; P.y = t.y - dir.y * dd; P.z = t.z - dir.z * dd;
        if (LosClear(t, P)) break;
        dd *= 0.75f;
    }
    Place(P, t);
}

void CamX_OnCameraButton()
{
    using namespace CamX;
    static auto last = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count() < 350) return;
    last = now;
    if (s_drone) SetDrone(false); else SetMode((s_mode + 1) % 4);
    FLog("CamX: kamera duymesi -> mode=%d drone=%d", s_mode, (int)s_drone);
}

// /mm menyusu ucun
int   CamX_GetMode()          { return CamX::s_mode; }
void  CamX_Cycle()            { CamX_OnCameraButton(); }
bool  CamX_IsDrone()          { return CamX::s_drone; }
void  CamX_ToggleDrone()      { CamX::SetDrone(!CamX::s_drone); }
float CamX_GetFov()           { return 70.0f + g_camxFovOffset; }
void  CamX_SetFov(float v)    { g_camxFovOffset = CamX::Clampf(v - 70.0f, -30.0f, 45.0f); }

bool CamX_Command(const char* a, const char* b, char* rep, int n)
{
    using namespace CamX;
    static const char* names[4] = { "Oyun kamerasi", "Chase (yaxinlasma)", "MTA (el ile)", "Birinci shexs" };
    auto eq = [&](const char* s) { return a && !strcmp(a, s); };
    if (!a || !a[0]) { SetDrone(false); SetMode((s_mode + 1) % 4); }
    else if (eq("game") || eq("off")) { SetDrone(false); SetMode(M_GAME); }
    else if (eq("chase"))             { SetDrone(false); SetMode(M_CHASE); }
    else if (eq("mta") || eq("pc"))   { SetDrone(false); SetMode(M_MTA); }
    else if (eq("fps"))               { SetDrone(false); SetMode(M_FPS); }
    else if (eq("drone"))             { SetDrone(!s_drone); snprintf(rep, n, "Dron: %s", s_drone ? "ACIQ" : "bagli"); return true; }
    else if (eq("speed"))             { s_dspeed = Clampf(b ? (float)atof(b) : 22.0f, 5.0f, 120.0f); snprintf(rep, n, "Dron sureti: %.0f", s_dspeed); return true; }
    else if (eq("fov"))               { const float v = b ? (float)atof(b) : 0.0f; if (v <= 0.0f) g_camxFovOffset = 0.0f; else CamX_SetFov(v); snprintf(rep, n, "FOV: %.0f", CamX_GetFov()); return true; }
    else if (eq("sens"))              { s_sens = Clampf(b ? (float)atof(b) : 5.0f, 1.0f, 20.0f) * 0.0009f; snprintf(rep, n, "Hessasliq: %.1f", s_sens / 0.0009f); return true; }
    else return false;
    snprintf(rep, n, "Kamera: %s", names[s_mode]);
    return true;
}
'''

# ---------------- gui.cpp ----------------
g = rd("gui/gui.cpp")
if "AZ_CAMX" in g:
    print("gui.cpp artıq yamalıdır, keçirəm")
else:
    shutil.copy(R + "gui/gui.cpp", R + "gui/gui.cpp.bak_camx")
    g = one(g, r"^namespace CamCtl", lambda m: "void CamX_Update();   // AZ_CAMX\nbool CamX_OnTouch(int type, int num, int x, int y);\nbool CamX_Command(const char* a, const char* b, char* rep, int n);\n\n" + m.group(0), "namespace CamCtl")
    if g.count("CamCtl_Update();") != 1:
        print("XƏTA: CamCtl_Update(); çağırışı 1 dənə deyil"); sys.exit(1)
    g = g.replace("CamCtl_Update();", "CamX_Update();")
    g = one(g, r"(^void CamCtl_Command\(const char\* a, const char\* b, char\* rep, int n\)[^\n]*\n\s*\{)",
            lambda m: m.group(1) + "\n        if (CamX_Command(a, b, rep, n)) return;   // AZ_CAMX", "CamCtl_Command")
    g = g.rstrip() + "\n" + CAMX
    wr("gui/gui.cpp", g)
    print("gui.cpp OK")

# ---------------- hooks.cpp ----------------
h = rd("game/hooks.cpp")
if "AZ_CAMX" in h:
    print("hooks.cpp artıq yamalıdır, keçirəm")
else:
    shutil.copy(R + "game/hooks.cpp", R + "game/hooks.cpp.bak_camx")
    h = one(h, r"^void CamCtl_OnTouch\(int type, int num, int x, int y\);[^\n]*$",
            lambda m: m.group(0) + "\nbool CamX_OnTouch(int type, int num, int x, int y);   // AZ_CAMX", "CamCtl_OnTouch decl")
    h = one(h, r"CamCtl_OnTouch\(type, num, posX, posY\);[^\n]*",
            lambda m: "if (CamX_OnTouch(type, num, posX, posY)) { AND_TouchEvent(1, num, posX, posY); return; }   // AZ_CAMX", "CamCtl_OnTouch call")
    h = one(h, r"^float \(\*CDraw__SetFOV\)\(float thiz, float a2\);",
            lambda m: "extern float g_camxFovOffset;   // AZ_CAMX\n" + m.group(0), "SetFOV decl")
    h = one(h, r"\+ thiz;\s*\n\s*if\(tmp > 100\) tmp = 100\.0;",
            lambda m: "+ thiz + g_camxFovOffset;\n    { const float cap = (g_camxFovOffset != 0.0f) ? 140.0f : 100.0f; if (tmp > cap) tmp = cap; if (tmp < 20.0f) tmp = 20.0f; }", "SetFOV body")
    wr("game/hooks.cpp", h)
    print("hooks.cpp OK")

# ---------------- pad.cpp ----------------
p = rd("game/pad.cpp")
if "AZ_CAMX" in p:
    print("pad.cpp artıq yamalıdır, keçirəm")
else:
    shutil.copy(R + "game/pad.cpp", R + "game/pad.cpp.bak_camx")
    HK = r'''// AZ_CAMX: kamera ikonu
#include <dlfcn.h>
void CamX_OnCameraButton();
uint32_t(*CPad__CycleCameraModeUp)(uintptr_t thiz);
uint32_t CPad__CycleCameraModeUp_hook(uintptr_t thiz)
{
    const uint32_t r = CPad__CycleCameraModeUp(thiz);
    if (r) CamX_OnCameraButton();
    return 0;   // oyunun oz kamera dovrunu yeyirik, bizimki isleyir
}
uint32_t(*CPad__CycleCameraModeDown)(uintptr_t thiz);
uint32_t CPad__CycleCameraModeDown_hook(uintptr_t thiz)
{
    const uint32_t r = CPad__CycleCameraModeDown(thiz);
    if (r) CamX_OnCameraButton();
    return 0;
}

'''
    INST = r'''    {   // AZ_CAMX
        void* hl = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
        const char* sUp = "_ZN4CPad25CycleCameraModeUpJustDownEv";
        const char* sDn = "_ZN4CPad27CycleCameraModeDownJustDownEv";
        if (hl && dlsym(hl, sUp)) CHook::InlineHook(sUp, &CPad__CycleCameraModeUp_hook, &CPad__CycleCameraModeUp);
        else FLog("CamX: %s tapilmadi", sUp);
        if (hl && dlsym(hl, sDn)) CHook::InlineHook(sDn, &CPad__CycleCameraModeDown_hook, &CPad__CycleCameraModeDown);
        else FLog("CamX: %s tapilmadi", sDn);
    }
'''
    p = one(p, r"^uint32_t\(\*CPad__CycleWeaponRightJustDown\)\(uintptr_t thiz\);", lambda m: HK + m.group(0), "CPad hook decl")
    p = one(p, r'^[^\n]*CHook::InlineHook\("_ZN4CPad24CycleWeaponRightJustDownEv"[^\n]*\n', lambda m: m.group(0) + INST, "CPad install")
    wr("game/pad.cpp", p)
    print("pad.cpp OK")

print("\nHazırdır. Backuplar: *.bak_camx. İndi build et.")
