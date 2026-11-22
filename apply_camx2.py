#!/usr/bin/env python3
import re, shutil, sys
R = "app/src/main/cpp/samp/"
def rd(p): return open(R + p, encoding="utf-8", errors="surrogateescape", newline="").read()
def wr(p, s): open(R + p, "w", encoding="utf-8", errors="surrogateescape", newline="").write(s)
def one(s, pat, fn, name, flags=re.M):
    r, n = re.subn(pat, fn, s, flags=flags)
    if n != 1:
        print("XETA: '%s' tapilmadi (n=%d) -> hec ne yazilmadi" % (name, n)); sys.exit(1)
    return r
def strip_diag(s):
    i = s.find("// CAMDIAG")
    if i < 0: return s
    j = s.rfind("{", 0, i); depth = 0; k = j
    while k < len(s):
        if s[k] == "{": depth += 1
        elif s[k] == "}":
            depth -= 1
            if depth == 0: break
        k += 1
    ls = s.rfind("\n", 0, j) + 1
    if s[ls:j].strip() == "": j = ls
    end = k + 1
    if s[end:end + 2] == "\r\n": end += 2
    elif s[end:end + 1] == "\n": end += 1
    return s[:j] + s[end:]

CAMX = r'''
// ============================================================================
// AZ_CAMX v2 : kamera sistemi (oyunun OZ kamerasinin ustunde, script komandasi yoxdur)
// ============================================================================
#include <dlfcn.h>
#include <chrono>
#include <cmath>
#include <cstring>
int LineOfSight(RwV3d* start, RwV3d* end, void* colpoint, uintptr_t ent, char buildings, char vehicles, char peds, char objects, char dummies, bool seeThrough, bool camera, bool unk);

namespace CamX
{
    enum { M_GAME = 0, M_MTA = 1, M_FPS = 2, MAXP = 8 };
    struct V3 { float x, y, z; };
    static V3 Mk(float x, float y, float z) { V3 v; v.x = x; v.y = y; v.z = z; return v; }
    static V3 Sub(V3 a, V3 b) { return Mk(a.x - b.x, a.y - b.y, a.z - b.z); }
    static V3 Add(V3 a, V3 b) { return Mk(a.x + b.x, a.y + b.y, a.z + b.z); }
    static V3 Mul(V3 a, float k) { return Mk(a.x * k, a.y * k, a.z * k); }
    static float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    static V3 Cross(V3 a, V3 b) { return Mk(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
    static V3 Norm(V3 a) { const float l = sqrtf(Dot(a, a)); return l < 1e-6f ? Mk(0.0f, 1.0f, 0.0f) : Mul(a, 1.0f / l); }
    static V3 Get(const CVector& c) { return Mk(c.x, c.y, c.z); }
    static void Put(CVector& c, V3 v) { c.x = v.x; c.y = v.y; c.z = v.z; }
    static float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static float WrapPi(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
    static CCamera& Cam() { return *reinterpret_cast<CCamera*>(g_libGTASA + (VER_x32 ? 0x00951FA8 : 0xBBA8D0)); }

    static int   s_mode = M_GAME, s_prevMode = M_GAME;
    static bool  s_drone = false, s_droneInit = false, s_needInit = true, s_haveT = false, s_pinching = false;
    static float s_yaw = 0.0f, s_pitch = -0.2f, s_extra = 0.0f, s_d0 = 5.0f, s_distMta = 7.5f;
    static float s_sens = 0.0045f, s_dspeed = 22.0f, s_lastH = 0.0f;
    static V3    s_T = { 0.0f, 0.0f, 0.0f }, s_dpos = { 0.0f, 0.0f, 0.0f };
    // oyunun oz kamera veziyyeti (bizim deyisiklik ona qayidib dolasmasin)
    static CCam* s_natCam = nullptr; static int s_natMode = -1;
    static V3    s_natSrc = { 0, 0, 0 }, s_natFront = { 0, 1, 0 }, s_natUp = { 0, 0, 1 };
    // toxunus
    static bool  p_down[MAXP] = {}, p_rz[MAXP] = {};
    static float p_x[MAXP] = {}, p_y[MAXP] = {};
    static int   s_look = -1, s_stick = -1, s_pa = -1, s_pb = -1;
    static float s_lx = 0, s_ly = 0, s_sx = 0, s_sy = 0, s_mx = 0, s_my = 0, s_pinchPrev = 0, s_pinchAcc = 0;

    static void ClampPitch()
    {
        const bool wide = (s_drone || s_mode == M_FPS);
        s_pitch = Clampf(s_pitch, wide ? -1.45f : -1.25f, wide ? 1.45f : 0.12f);
    }
    static void SetMode(int m)
    {
        if (m == s_mode) return;
        const int old = s_mode;
        s_mode = m; s_needInit = true;
        if (m == M_FPS) s_prevMode = (old == M_FPS) ? (int)M_GAME : old;
    }
    static void SetDrone(bool on)
    {
        if (on == s_drone) return;
        s_drone = on; s_droneInit = on; s_stick = -1; s_mx = s_my = 0.0f; s_needInit = true;
    }
    static void ApplyPinch(float delta)          // delta>0 : barmaqlar aralanir = yaxinlasdir
    {
        if (s_drone) { s_dspeed = Clampf(s_dspeed + delta * 0.06f, 5.0f, 120.0f); return; }
        if (s_mode == M_FPS) {
            if (delta < 0.0f) {
                s_pinchAcc += delta;
                if (s_pinchAcc < -70.0f) {
                    s_pinchAcc = 0.0f;
                    if (s_prevMode == M_MTA) { s_distMta = 2.6f; SetMode(M_MTA); }
                    else { s_extra = 2.4f - s_d0; SetMode(M_GAME); }
                }
            } else s_pinchAcc = 0.0f;
            return;
        }
        const float k = 0.02f;
        if (s_mode == M_MTA) {
            s_distMta -= delta * k * (1.0f + s_distMta * 0.15f);
            if (s_distMta < 1.3f) { s_distMta = 2.6f; SetMode(M_FPS); s_pinchAcc = 0.0f; return; }
            s_distMta = Clampf(s_distMta, 1.3f, 25.0f);
            return;
        }
        float dn = s_d0 + s_extra;               // oyun kamerasi: native mesafe + elave
        dn -= delta * k * (1.0f + dn * 0.15f);
        if (dn < 1.3f) { s_extra = 2.4f - s_d0; SetMode(M_FPS); s_pinchAcc = 0.0f; return; }
        s_extra = Clampf(dn, 1.3f, 16.0f) - s_d0;
    }
    static bool LosClear(V3 a, V3 b)
    {
        RwV3d s = { a.x, a.y, a.z }, e = { b.x, b.y, b.z };
        alignas(16) char col[256]; uintptr_t hitEnt = 0;
        return LineOfSight(&s, &e, col, (uintptr_t)&hitEnt, 1, 0, 0, 0, 0, false, true, false) == 0;
    }
    static bool GetTarget(CPedGTA*& g, V3& t, float& h, bool& inVeh)
    {
        CPlayerPed* p = pGame ? pGame->FindPlayerPed() : nullptr;
        if (!p || !p->m_pPed) return false;
        g = p->m_pPed;
        inVeh = g->IsInVehicle();
        if (inVeh) {
            CVehicleGTA* v = g->pVehicle;
            const RwMatrix m = v->GetMatrix().ToRwMatrix();
            h = atan2f(m.up.y, m.up.x); t = Mk(m.pos.x, m.pos.y, m.pos.z + 0.9f);
        } else {
            const RwMatrix m = g->GetMatrix().ToRwMatrix();
            h = atan2f(m.up.y, m.up.x); t = Mk(m.pos.x, m.pos.y, m.pos.z + 0.75f);
        }
        return true;
    }
    static void SetView(CCam* c, V3 src, V3 front)
    {
        front = Norm(front);
        V3 right = Cross(front, Mk(0.0f, 0.0f, 1.0f));
        if (Dot(right, right) < 1e-6f) right = Mk(1.0f, 0.0f, 0.0f);
        right = Norm(right);
        Put(c->Source, src); Put(c->Front, front); Put(c->Up, Cross(right, front));
    }

    static void PostProcess(CCam* c)
    {
        CCamera& TC = Cam();
        if (c != &TC.m_aCams[TC.m_nActiveCam]) return;
        s_natCam = c; s_natMode = (int)c->m_nMode;
        s_natSrc = Get(c->Source); s_natFront = Get(c->Front); s_natUp = Get(c->Up);
        if (!pGame || !pNetGame || pGame->IsGamePaused()) return;

        static auto last = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count(); last = now;
        dt = Clampf(dt, 0.0f, 0.05f);

        CPedGTA* g = nullptr; V3 T; float h = 0.0f; bool inVeh = false;
        if (!GetTarget(g, T, h, inVeh)) return;
        const float dH = WrapPi(h - s_lastH); s_lastH = h;
        if (!s_haveT || sqrtf(Dot(Sub(T, s_T), Sub(T, s_T))) > 8.0f) { s_T = T; s_haveT = true; }
        else s_T = Add(s_T, Mul(Sub(T, s_T), Clampf(dt * 14.0f, 0.0f, 1.0f)));

        const V3 curSrc = s_natSrc, curFront = Norm(s_natFront);

        if (s_drone) {                                     // ---------------- DRON
            if (s_droneInit) {
                s_dpos = curSrc; s_yaw = atan2f(curFront.y, curFront.x);
                s_pitch = asinf(Clampf(curFront.z, -1.0f, 1.0f)); s_droneInit = false; ClampPitch();
            }
            const float cp = cosf(s_pitch), sp = sinf(s_pitch), cy = cosf(s_yaw), sy = sinf(s_yaw);
            const V3 dir = Mk(cp * cy, cp * sy, sp);
            const float fwd = -s_my, str = s_mx, sp2 = s_dspeed * dt;
            s_dpos = Add(s_dpos, Mk((dir.x * fwd + sy * str) * sp2, (dir.y * fwd - cy * str) * sp2, dir.z * fwd * sp2));
            V3 e = Sub(s_dpos, T); const float dd = sqrtf(Dot(e, e));
            if (dd > 300.0f) s_dpos = Add(T, Mul(e, 300.0f / dd));
            SetView(c, s_dpos, dir);
            return;
        }

        const int nm = (int)c->m_nMode;                    // nisan, snayper, script kamera = oyunun oz kamerasi
        if (!(nm == 4 || nm == 18 || nm == 3 || nm == 22)) return;

        if (s_mode == M_MTA) {                             // ---------------- MTA (PC) kamerasi
            if (s_needInit) {
                s_yaw = atan2f(curFront.y, curFront.x);
                s_pitch = asinf(Clampf(curFront.z, -1.0f, 1.0f)); s_needInit = false; ClampPitch();
            }
            const float cp = cosf(s_pitch), sp = sinf(s_pitch);
            const V3 dir = Mk(cp * cosf(s_yaw), cp * sinf(s_yaw), sp);
            float dd = s_distMta; V3 P = Sub(s_T, Mul(dir, dd));
            for (int i = 0; i < 6 && !LosClear(s_T, P); ++i) { dd *= 0.75f; P = Sub(s_T, Mul(dir, dd)); }
            SetView(c, P, dir);
            return;
        }

        if (s_mode == M_FPS) {                             // ---------------- birinci shexs (bashin icinden)
            if (s_needInit) {
                s_yaw = atan2f(curFront.y, curFront.x);
                s_pitch = asinf(Clampf(curFront.z, -1.0f, 1.0f)); s_needInit = false; ClampPitch();
            }
            if (inVeh) s_yaw += dH;                        // masin donende baxis da donsun
            RwV3d bp = { 0.0f, 0.0f, 0.0f };
            if (g) g->GetBonePosition(&bp, 5, false);      // 5 = bash
            if (bp.x == 0.0f && bp.y == 0.0f && bp.z == 0.0f) { bp.x = T.x; bp.y = T.y; bp.z = T.z - 0.1f; }
            const float cp = cosf(s_pitch), sp = sinf(s_pitch);
            const V3 dir = Mk(cp * cosf(s_yaw), cp * sinf(s_yaw), sp);
            SetView(c, Mk(bp.x + dir.x * 0.12f, bp.y + dir.y * 0.12f, bp.z + dir.z * 0.12f + 0.03f), dir);
            return;
        }

        // ---------------- OYUN kamerasi: oyunun oz kamerasi + hamar elave yaxinlasma/uzaqlasma
        const float d0 = Dot(Sub(T, curSrc), curFront);
        s_d0 = d0;
        if (d0 < 0.8f || fabsf(s_extra) < 0.01f) return;
        float ex = s_extra;
        if (d0 + ex < 1.0f) ex = 1.0f - d0;
        V3 P = Sub(curSrc, Mul(curFront, ex));
        for (int i = 0; ex > 0.0f && i < 5 && !LosClear(T, P); ++i) { ex *= 0.7f; P = Sub(curSrc, Mul(curFront, ex)); }
        Put(c->Source, P);                                 // Front/Up oyunun oz qalir
    }
}

static void (*CCam_Process_orig)(CCam*);
static void CCam_Process_hook(CCam* self)
{
    using namespace CamX;
    if (self == s_natCam && (int)self->m_nMode == s_natMode) {   // oyun oz evvelki veziyyetini gorsun
        Put(self->Source, s_natSrc); Put(self->Front, s_natFront); Put(self->Up, s_natUp);
    }
    CCam_Process_orig(self);
    PostProcess(self);
}

void CamX_OnCameraButton()
{
    using namespace CamX;
    static auto last = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count() < 350) return;
    last = now;
    if (s_drone) SetDrone(false); else SetMode((s_mode + 1) % 3);
}

static uint32_t (*CPad_CycleCamDown_orig)(uintptr_t thiz);
static uint32_t CPad_CycleCamDown_hook(uintptr_t thiz)
{
    const uint32_t r = CPad_CycleCamDown_orig(thiz);
    if (r) CamX_OnCameraButton();
    return 0;      // oyunun oz kamera dovru yeyilir, bizimki isleyir
}

void CamX_InstallHooks()
{
    void* hl = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
    const char* sProc = "_ZN4CCam7ProcessEv";
    const char* sDn = "_ZN4CPad27CycleCameraModeDownJustDownEv";
    if (hl && dlsym(hl, sProc)) CHook::InlineHook(sProc, &CCam_Process_hook, &CCam_Process_orig);
    else FLog("CamX: %s tapilmadi", sProc);
    if (hl && dlsym(hl, sDn)) CHook::InlineHook(sDn, &CPad_CycleCamDown_hook, &CPad_CycleCamDown_orig);
    else FLog("CamX: %s tapilmadi", sDn);
}

bool CamX_OnTouch(int type, int num, int x, int y)
{
    using namespace CamX;
    if (num < 0 || num >= MAXP) return false;
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    const float fx = (float)x, fy = (float)y;
    const bool ownLook = (s_mode != M_GAME) || s_drone;
    bool consume = false;

    if (type == 2) {                                       // push
        p_down[num] = true; p_x[num] = fx; p_y[num] = fy;
        p_rz[num] = fx > d.x * 0.35f && fy < d.y * 0.72f;
        if (s_drone && fx < d.x * 0.35f && fy > d.y * 0.30f && s_stick < 0) { s_stick = num; s_sx = fx; s_sy = fy; s_mx = s_my = 0.0f; }
        if (num == s_stick) consume = true;
        else if (p_rz[num] && s_look < 0 && ownLook && !s_pinching) { s_look = num; s_lx = fx; s_ly = fy; }
    }
    else if (type == 3) {                                  // move
        p_x[num] = fx; p_y[num] = fy;
        if (num == s_stick) {
            const float Rr = d.y * 0.16f;
            s_mx = Clampf((fx - s_sx) / Rr, -1.0f, 1.0f); s_my = Clampf((fy - s_sy) / Rr, -1.0f, 1.0f);
            consume = true;
        } else if (num == s_look && !s_pinching) {
            s_yaw -= (fx - s_lx) * s_sens;                 // saga surusdur -> saga bax
            s_pitch -= (fy - s_ly) * s_sens;
            s_lx = fx; s_ly = fy; ClampPitch();
        } else if (s_look < 0 && p_rz[num] && ownLook && !s_pinching) { s_look = num; s_lx = fx; s_ly = fy; }
    }
    else if (type == 1) {                                  // pop
        if (num == s_stick) { s_stick = -1; s_mx = s_my = 0.0f; consume = true; }
        if (num == s_look) s_look = -1;
        if (s_pinching && (num == s_pa || num == s_pb)) consume = true;
        p_down[num] = false; p_rz[num] = false;
    }

    if (type != 1) {                                       // iki barmaqla yaxinlasdirma
        int a = -1, b = -1;
        for (int i = 0; i < MAXP; ++i) if (p_down[i] && p_rz[i]) { if (a < 0) a = i; else if (b < 0) b = i; }
        if (a >= 0 && b >= 0) {
            if (!s_pinching) { s_pinching = true; s_pa = a; s_pb = b; s_pinchPrev = hypotf(p_x[a] - p_x[b], p_y[a] - p_y[b]); s_pinchAcc = 0.0f; s_look = -1; }
            else if (type == 3 && (num == s_pa || num == s_pb)) {
                const float dd = hypotf(p_x[s_pa] - p_x[s_pb], p_y[s_pa] - p_y[s_pb]);
                const float dl = dd - s_pinchPrev; s_pinchPrev = dd; ApplyPinch(dl);
            }
            if (num == s_pa || num == s_pb) consume = true;   // oyunun oz zoomu qarismasin
        } else s_pinching = false;
    } else {
        int cnt = 0;
        for (int i = 0; i < MAXP; ++i) if (p_down[i] && p_rz[i]) ++cnt;
        if (cnt < 2) s_pinching = false;
    }
    return consume;
}

// ---- /mm menyusu ucun
int   CamX_GetMode()       { return CamX::s_mode; }
void  CamX_Cycle()         { if (CamX::s_drone) CamX::SetDrone(false); else CamX::SetMode((CamX::s_mode + 1) % 3); }
bool  CamX_IsDrone()       { return CamX::s_drone; }
void  CamX_ToggleDrone()   { CamX::SetDrone(!CamX::s_drone); }
float CamX_GetFov()        { return 70.0f + g_camxFovOffset; }
void  CamX_SetFov(float v) { g_camxFovOffset = CamX::Clampf(v - 70.0f, -30.0f, 45.0f); }

bool CamX_Command(const char* a, const char* b, char* rep, int n)
{
    using namespace CamX;
    static const char* names[3] = { "Oyun kamerasi", "MTA (el ile)", "Birinci shexs" };
    auto eq = [&](const char* s) { return a && !strcmp(a, s); };
    if (!a || !a[0])                                { SetDrone(false); SetMode((s_mode + 1) % 3); }
    else if (eq("game") || eq("off") || eq("chase")) { SetDrone(false); SetMode(M_GAME); s_extra = 0.0f; }
    else if (eq("mta") || eq("pc"))                  { SetDrone(false); SetMode(M_MTA); }
    else if (eq("fps"))                              { SetDrone(false); SetMode(M_FPS); }
    else if (eq("drone"))  { SetDrone(!s_drone); snprintf(rep, n, "Dron: %s", s_drone ? "ACIQ" : "bagli"); return true; }
    else if (eq("speed"))  { s_dspeed = Clampf(b ? (float)atof(b) : 22.0f, 5.0f, 120.0f); snprintf(rep, n, "Dron sureti: %.0f", s_dspeed); return true; }
    else if (eq("fov"))    { const float v = b ? (float)atof(b) : 0.0f; if (v <= 0.0f) g_camxFovOffset = 0.0f; else CamX_SetFov(v); snprintf(rep, n, "FOV: %.0f", CamX_GetFov()); return true; }
    else if (eq("sens"))   { s_sens = Clampf(b ? (float)atof(b) : 5.0f, 1.0f, 20.0f) * 0.0009f; snprintf(rep, n, "Hessasliq: %.1f", s_sens / 0.0009f); return true; }
    else return false;
    snprintf(rep, n, "Kamera: %s", names[s_mode]);
    return true;
}
'''

DECL = '''void CamX_OnTouchDummy();   // AZ_CAMX
bool CamX_Command(const char* a, const char* b, char* rep, int n);
int   CamX_GetMode();
void  CamX_Cycle();
bool  CamX_IsDrone();
void  CamX_ToggleDrone();
float CamX_GetFov();
void  CamX_SetFov(float v);

'''.replace("void CamX_OnTouchDummy();   // AZ_CAMX\n", "// AZ_CAMX\n")

UI = r'''                        {   // AZ_CAMX: kamera paneli
                                const float y0 = P.y + S(124.0f + 34.0f * n + 14.0f);
                                static const char* kMn[3] = { "Oyun kamerasi", "MTA (el ile)", "Birinci shexs" };
                                const Rc rA{ cx0, y0, cw, S(36.0f) };
                                Fill(rA, Col(27, 30, 40, 255), S(9.0f));
                                Txt(S(15.0f), rA.x + S(16.0f), rA.y + (rA.h - S(15.0f)) * 0.5f, Col(200, 204, 216), "Kamera");
                                Txt(S(15.0f), rA.x + S(120.0f), rA.y + (rA.h - S(15.0f)) * 0.5f, Accent(0.0f), kMn[CamX_GetMode() % 3]);
                                const Rc bA{ rA.x + rA.w - S(112.0f), rA.y + S(5.0f), S(100.0f), S(26.0f) };
                                Fill(bA, Col(60, 66, 84, 255), S(7.0f));
                                Txt(S(14.0f), bA.x + S(22.0f), bA.y + (bA.h - S(14.0f)) * 0.5f, Col(240, 243, 250), "Deyish");
                                if (Tap(240, bA, canInput)) CamX_Cycle();

                                const Rc rB{ cx0, y0 + S(40.0f), cw, S(36.0f) };
                                Fill(rB, Col(27, 30, 40, 255), S(9.0f));
                                Txt(S(15.0f), rB.x + S(16.0f), rB.y + (rB.h - S(15.0f)) * 0.5f, Col(200, 204, 216), "Dron kamera");
                                Txt(S(15.0f), rB.x + S(120.0f), rB.y + (rB.h - S(15.0f)) * 0.5f, Accent(0.0f), CamX_IsDrone() ? "ACIQ" : "bagli");
                                const Rc bB{ rB.x + rB.w - S(112.0f), rB.y + S(5.0f), S(100.0f), S(26.0f) };
                                Fill(bB, CamX_IsDrone() ? Col(70, 130, 90, 255) : Col(60, 66, 84, 255), S(7.0f));
                                Txt(S(14.0f), bB.x + S(24.0f), bB.y + (bB.h - S(14.0f)) * 0.5f, Col(240, 243, 250), CamX_IsDrone() ? "Bagla" : "Ac");
                                if (Tap(241, bB, canInput)) CamX_ToggleDrone();

                                const Rc rC{ cx0, y0 + S(80.0f), cw, S(40.0f) };
                                Fill(rC, Col(27, 30, 40, 255), S(9.0f));
                                char fb[32]; snprintf(fb, sizeof(fb), "FOV  %.0f", CamX_GetFov());
                                Txt(S(15.0f), rC.x + S(16.0f), rC.y + (rC.h - S(15.0f)) * 0.5f, Col(200, 204, 216), fb);
                                const float tx0 = rC.x + S(120.0f), tx1 = rC.x + rC.w - S(24.0f), ty = rC.y + rC.h * 0.5f;
                                static bool s_fovDrag = false;
                                const bool inHit = g_mmTouch.x >= tx0 - S(12.0f) && g_mmTouch.x <= tx1 + S(12.0f) &&
                                                   g_mmTouch.y >= rC.y && g_mmTouch.y <= rC.y + rC.h;
                                if (g_mmTouch.pressed && inHit && canInput) s_fovDrag = true;
                                if (!g_mmTouch.down) s_fovDrag = false;
                                if (s_fovDrag) {
                                        float f = (g_mmTouch.x - tx0) / (tx1 - tx0);
                                        f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
                                        CamX_SetFov(40.0f + 70.0f * f);
                                }
                                float ff = (CamX_GetFov() - 40.0f) / 70.0f;
                                ff = ff < 0.0f ? 0.0f : (ff > 1.0f ? 1.0f : ff);
                                Fill(Rc{ tx0, ty - S(3.0f), tx1 - tx0, S(6.0f) }, Col(255, 255, 255, 40), S(3.0f));
                                Fill(Rc{ tx0, ty - S(3.0f), (tx1 - tx0) * ff, S(6.0f) }, Col(235, 235, 235, 255), S(3.0f));
                                Fill(Rc{ tx0 + (tx1 - tx0) * ff - S(8.0f), ty - S(8.0f), S(16.0f), S(16.0f) }, Col(255, 255, 255, 255), S(8.0f));
                        }'''

h = rd("game/hooks.cpp"); g = rd("gui/gui.cpp")
if "AZ_CAMX" in h or "AZ_CAMX" in g:
    print("Artiq yamalidir."); sys.exit(0)

h = strip_diag(h)
h = one(h, r"^void CamCtl_OnTouch\(int type, int num, int x, int y\);[^\n]*$",
        lambda m: m.group(0) + "\nbool CamX_OnTouch(int type, int num, int x, int y);   // AZ_CAMX\nvoid CamX_InstallHooks();", "hooks decl")
h = one(h, r"CamCtl_OnTouch\(type, num, posX, posY\);[^\n]*",
        lambda m: "if (CamX_OnTouch(type, num, posX, posY)) { AND_TouchEvent(1, num, posX, posY); return; }   // AZ_CAMX", "hooks call")
h = one(h, r"^float \(\*CDraw__SetFOV\)\(float thiz, float a2\);",
        lambda m: "float g_camxFovOffset = 0.0f;   // AZ_CAMX\n" + m.group(0), "FOV decl")
h = one(h, r"\+ thiz;\s*\n\s*if\(tmp > 100\) tmp = 100\.0;",
        lambda m: "+ thiz + g_camxFovOffset;\n    { const float cap = (g_camxFovOffset != 0.0f) ? 140.0f : 100.0f; if (tmp > cap) tmp = cap; if (tmp < 20.0f) tmp = 20.0f; }", "FOV body")
h = one(h, r'^[^\n]*"_ZN5CDraw6SetFOVEfb"[^\n]*\n', lambda m: m.group(0) + "    CamX_InstallHooks();   // AZ_CAMX\n", "install")
h = h.rstrip() + "\n" + CAMX + "\n"

g = one(g, r"^namespace CamCtl", lambda m: DECL + m.group(0), "namespace CamCtl")
if g.count("CamCtl_Update();") != 1: print("XETA: CamCtl_Update(); 1 dene deyil"); sys.exit(1)
g = g.replace("CamCtl_Update();", "/* AZ_CAMX: eski /cam sondurulub */")
g = one(g, r"(^void CamCtl_Command\(const char\* a, const char\* b, char\* rep, int n\)[^\n]*\n\s*\{)",
        lambda m: m.group(1) + "\n        if (CamX_Command(a, b, rep, n)) return;   // AZ_CAMX", "CamCtl_Command")
g = one(g, r"S\(124\.0f \+ 52\.0f \* i\), cw, S\(44\.0f\)", lambda m: "S(124.0f + 34.0f * i), cw, S(30.0f)", "komanda setri")
g = one(g, r'\{ "/yt",\s*"YouTube paneli" \},',
        lambda m: m.group(0) + '\n                                { "/cam",     "mta | fps | game | drone | fov N" },', "komanda siyahisi")
g = one(g, r"(kCmds\[i\]\.desc\);\s*\n\s*\})", lambda m: m.group(1) + "\n" + UI, "kamera paneli")

shutil.copy(R + "game/hooks.cpp", R + "game/hooks.cpp.bak_camx2")
shutil.copy(R + "gui/gui.cpp", R + "gui/gui.cpp.bak_camx2")
wr("game/hooks.cpp", h); wr("gui/gui.cpp", g)
print("OK - hooks.cpp ve gui.cpp yamandi. Build et.")
