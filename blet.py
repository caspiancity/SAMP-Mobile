# -*- coding: utf-8 -*-
#!/usr/bin/env python3
# apply_v2.py  --  YENIDEN YAZILMIS, TEK PATCH (temiz baza ustunde)
#   Menyu (/mm): iOS stili shushe panel, yay animasiyasi, scroll siyahi
#       Masin | Handling | Far | Spidometr
#       Handling: butun sahelere toxun -> deyeri yaz; 5 slot; kod (fayla), koddan yukle, standarta qaytar
#   Silinir: profil, nomre (/nomre, /dump), /tg, /cam, faiz-slider handling, nitro/hidravlik
#   Launcher: Settings-de "handling.cfg sec" (oyun bunu acilanda yukleyir), arxa fon musiqisi, yeni loading animasiyasi
#   Oyun: nametag-da AZ bayragi, crash.log / perf.log (SAMP qovlugunda)
import os, re, shutil, subprocess

ROOT = os.getcwd()
CPP  = os.path.join(ROOT, "app/src/main/cpp/samp")
JAVA = os.path.join(ROOT, "app/src/main/java/com/samp/mobile/launcher")
RES  = os.path.join(ROOT, "app/src/main/res")

CRASH_BLOCK = r'''
// ===== CRASHLOG_PATCH13 / STABIL_PATCH13 =====
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <unwind.h>
#include <ucontext.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <chrono>

namespace crashlog
{
    struct BtState { void** cur; void** end; };

    static _Unwind_Reason_Code UnwindCb(struct _Unwind_Context* ctx, void* arg)
    {
        BtState* s = (BtState*)arg;
        const uintptr_t pc = (uintptr_t)_Unwind_GetIP(ctx);
        if (pc) {
            if (s->cur == s->end) return _URC_END_OF_STACK;
            *s->cur++ = (void*)pc;
        }
        return _URC_NO_REASON;
    }

    static char g_path[300] = "";
    static struct sigaction g_old[NSIG];
    static volatile sig_atomic_t g_in = 0;
    static const int kSigs[] = { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSYS };

    static void Out(int fd, const char* s) { if (fd >= 0) { ssize_t r = write(fd, s, strlen(s)); (void)r; } }

    static void Frame(int fd, int idx, uintptr_t pc)
    {
        char line[420];
        Dl_info di;
        memset(&di, 0, sizeof(di));
        if (dladdr((void*)pc, &di) && di.dli_fname) {
            const char* base = strrchr(di.dli_fname, '/');
            base = base ? base + 1 : di.dli_fname;
            snprintf(line, sizeof(line), "  #%02d  %s + 0x%lx   [%s]\n", idx, base,
                     (unsigned long)(pc - (uintptr_t)di.dli_fbase), di.dli_sname ? di.dli_sname : "?");
        } else {
            snprintf(line, sizeof(line), "  #%02d  pc 0x%lx (modul tapilmadi)\n", idx, (unsigned long)pc);
        }
        Out(fd, line);
    }

    static void Handler(int sig, siginfo_t* info, void* uc)
    {
        if (!g_in) {
            g_in = 1;
            const int fd = open(g_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd >= 0) {
                char buf[360];
                snprintf(buf, sizeof(buf), "\n===== CRASH signal=%d code=%d addr=%p time=%ld build=%s %s =====\n",
                         sig, info ? info->si_code : 0, info ? info->si_addr : nullptr, (long)time(nullptr), __DATE__, __TIME__);
                Out(fd, buf);

                uintptr_t pc = 0, lr = 0;
                ucontext_t* c = (ucontext_t*)uc;
#if defined(__aarch64__)
                if (c) { pc = (uintptr_t)c->uc_mcontext.pc; lr = (uintptr_t)c->uc_mcontext.regs[30]; }
#elif defined(__arm__)
                if (c) { pc = (uintptr_t)c->uc_mcontext.arm_pc; lr = (uintptr_t)c->uc_mcontext.arm_lr; }
#elif defined(__x86_64__)
                if (c) { pc = (uintptr_t)c->uc_mcontext.gregs[REG_RIP]; }
#endif
                Out(fd, "cokme yeri (PC) ve LR:\n");
                Frame(fd, 0, pc);
                if (lr) Frame(fd, 1, lr);

                Out(fd, "backtrace:\n");
                void* frames[40];
                BtState st{ frames, frames + 40 };
                _Unwind_Backtrace(UnwindCb, &st);
                for (void** p = frames; p < st.cur; ++p) Frame(fd, (int)(p - frames), (uintptr_t)*p);
                close(fd);
            }
        }

        // evvelki handler-e ver (Firebase ve s.)
        const struct sigaction& o = g_old[sig];
        if (o.sa_flags & SA_SIGINFO) {
            if (o.sa_sigaction) { o.sa_sigaction(sig, info, uc); return; }
        } else if (o.sa_handler != SIG_DFL && o.sa_handler != SIG_IGN && o.sa_handler) {
            o.sa_handler(sig);
            return;
        }
        signal(sig, SIG_DFL);
        raise(sig);
    }

    static void Install(const char* path)
    {
        snprintf(g_path, sizeof(g_path), "%s", path);
        static char altStack[64 * 1024];
        stack_t ss;
        ss.ss_sp = altStack;
        ss.ss_size = sizeof(altStack);
        ss.ss_flags = 0;
        sigaltstack(&ss, nullptr);

        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = Handler;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        for (size_t i = 0; i < sizeof(kSigs) / sizeof(kSigs[0]); ++i)
            sigaction(kSigs[i], &sa, &g_old[kSigs[i]]);
    }
}

// 150ms-den uzun kadrlari SAMP/perf.log-a yazir (CGame::Process her kadr cagirir)
void PerfLog_Frame()
{
    using namespace std::chrono;
    static steady_clock::time_point last = steady_clock::now();
    static int written = 0;
    const steady_clock::time_point now = steady_clock::now();
    const float ms = duration<float, std::milli>(now - last).count();
    last = now;
    if (ms < 150.0f || ms > 4000.0f || written >= 300 || !g_pszStorage) return;

    char path[0x100];
    snprintf(path, sizeof(path), "%sSAMP/perf.log", g_pszStorage);
    FILE* f = fopen(path, "a");
    if (!f) return;
    fprintf(f, "%ld  kadr=%.0fms\n", (long)time(nullptr), ms);
    fclose(f);
    ++written;
}
// ===== /CRASHLOG_PATCH13 =====
'''

SPLASH_ADD = r'''
        // ===== SPLASH_ANIM_PATCH8: yeni loading animasiyalari =====
        try {
            final android.view.View hRoot = findViewById(R.id.main_splash_layout);
            final android.view.View hLogo = findViewById(R.id.imageView);
            final TextView hLoad = (TextView) findViewById(R.id.textView2);
            if (hRoot != null) {
                hRoot.setAlpha(0f);
                hRoot.animate().alpha(1f).setDuration(700).start();
            }
            if (hLogo != null) {
                String[] props = {"scaleX", "scaleY"};
                for (String pr : props) {
                    android.animation.ObjectAnimator a = android.animation.ObjectAnimator.ofFloat(hLogo, pr, 1f, 1.12f);
                    a.setDuration(900);
                    a.setRepeatCount(android.animation.ValueAnimator.INFINITE);
                    a.setRepeatMode(android.animation.ValueAnimator.REVERSE);
                    a.setInterpolator(new android.view.animation.AccelerateDecelerateInterpolator());
                    a.start();
                }
                android.animation.ObjectAnimator g = android.animation.ObjectAnimator.ofFloat(hLogo, "alpha", 0.75f, 1f);
                g.setDuration(900);
                g.setRepeatCount(android.animation.ValueAnimator.INFINITE);
                g.setRepeatMode(android.animation.ValueAnimator.REVERSE);
                g.start();
            }
            if (hLoad != null) {
                final Handler hH = new Handler();
                hH.post(new Runnable() {
                    int i = 0;
                    @Override public void run() {
                        if (isFinishing() || isDestroyed()) return;
                        StringBuilder sb = new StringBuilder("Loading");
                        for (int k = 0; k < (i % 4); k++) sb.append('.');
                        hLoad.setText(sb.toString());
                        i++;
                        hH.postDelayed(this, 400);
                    }
                });
            }
        } catch (Throwable ignored) {}
'''

MUSIC = r'''
    // ===== LOBBY_MUSIC_PATCH8: arxa fon musiqisi (res/raw/bg_music1, bg_music2, bg_music3) =====
    private android.media.MediaPlayer bgMusic;

    private void startBgMusic() {
        try {
            if (bgMusic != null) {
                if (!bgMusic.isPlaying()) bgMusic.start();
                return;
            }
            int[] ids = new int[3];
            int n = 0;
            for (int i = 1; i <= 3; i++) {
                int id = getResources().getIdentifier("bg_music" + i, "raw", getPackageName());
                if (id != 0) ids[n++] = id;
            }
            if (n == 0) return;
            int pick = ids[new java.util.Random().nextInt(n)];
            bgMusic = android.media.MediaPlayer.create(this, pick);
            if (bgMusic == null) return;
            bgMusic.setVolume(0.6f, 0.6f);
            bgMusic.setOnCompletionListener(new android.media.MediaPlayer.OnCompletionListener() {
                @Override public void onCompletion(android.media.MediaPlayer mp) {
                    releaseBgMusic();
                    startBgMusic();   // novbeti tesaduf mahni
                }
            });
            bgMusic.start();
        } catch (Throwable t) {
            bgMusic = null;
        }
    }

    private void pauseBgMusic() {
        try { if (bgMusic != null && bgMusic.isPlaying()) bgMusic.pause(); } catch (Throwable ignored) {}
    }

    private void releaseBgMusic() {
        try { if (bgMusic != null) { bgMusic.release(); } } catch (Throwable ignored) {}
        bgMusic = null;
    }

    @Override
    protected void onPause() {
        super.onPause();
        pauseBgMusic();
    }

    @Override
    protected void onDestroy() {
        releaseBgMusic();
        super.onDestroy();
    }
    // ===== /LOBBY_MUSIC_PATCH8 =====

'''

FLAG_HELPER = r'''// ===== AZFLAG_PATCH9 =====
static void DrawAzFlag(ImDrawList* dl, ImVec2 p, float w, float h)
{
	float s = h / 3.0f;
	dl->AddRectFilled(p, ImVec2(p.x + w, p.y + s), IM_COL32(0, 181, 226, 255));
	dl->AddRectFilled(ImVec2(p.x, p.y + s), ImVec2(p.x + w, p.y + 2 * s), IM_COL32(239, 51, 64, 255));
	dl->AddRectFilled(ImVec2(p.x, p.y + 2 * s), ImVec2(p.x + w, p.y + h), IM_COL32(80, 158, 47, 255));
	ImVec2 c(p.x + w * 0.44f, p.y + h * 0.5f);
	float r = h * 0.2f;
	dl->AddCircleFilled(c, r, IM_COL32(255, 255, 255, 255), 14);
	dl->AddCircleFilled(ImVec2(c.x + r * 0.35f, c.y), r * 0.8f, IM_COL32(239, 51, 64, 255), 14);
	dl->AddCircleFilled(ImVec2(c.x + r * 1.55f, c.y), r * 0.28f, IM_COL32(255, 255, 255, 255), 8);
	dl->AddRect(p, ImVec2(p.x + w, p.y + h), IM_COL32(0, 0, 0, 150));
}

'''

FLAG_NEWNAME = r'''// AZFLAG_PATCH9: bayraq + ad, tund yarimseffaf panel icinde
	{
		const float fs = UISettings::fontSize() / 2;
		const ImVec2 ts = renderer->calculateTextSize(szNick, fs);
		const float flagH = fs * 0.95f;
		const float flagW = flagH * 1.5f;
		const float gap = fs * 0.35f;
		const float pad = fs * 0.3f;
		const float totalW = flagW + gap + ts.x;
		const float x0 = pos.x - totalW / 2;

		ImDrawList* dl = ImGui::GetBackgroundDrawList();
		dl->AddRectFilled(ImVec2(x0 - pad, pos.y - 1.0f), ImVec2(x0 + totalW + pad, pos.y + ts.y + 1.0f),
		                  IM_COL32(0, 0, 0, 110), fs * 0.4f);
		DrawAzFlag(dl, ImVec2(x0, pos.y + (ts.y - flagH) / 2), flagW, flagH);
		renderer->drawText(ImVec2(x0 + flagW + gap, pos.y), __builtin_bswap32(dwColor | (0x000000FF)),
		                   std::string(szNick), true, fs);
	}
'''

HND_BLOCK = r'''
// ===== HNDV2: handling redaktoru backend (menyu + slotlar + kod) =====
#include <vector>
#include <map>
#include <string>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <strings.h>
#include "tHandlingData.h"

extern char* g_pszStorage;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"

struct HndxField { const char* name; size_t off; };
#define HNDX(n, m) { n, offsetof(tHandlingData, m) }
static const HndxField g_HndxFields[] = {
    HNDX("mass", m_fMass),
    HNDX("turnmass", m_fTurnMass),
    HNDX("drag", m_fDragMult),
    { "comx", offsetof(tHandlingData, m_vecCentreOfMass) },
    { "comy", offsetof(tHandlingData, m_vecCentreOfMass) + 4 },
    { "comz", offsetof(tHandlingData, m_vecCentreOfMass) + 8 },
    HNDX("buoyancy", m_fBuoyancyConstant),
    HNDX("traction", m_fTractionMultiplier),
    { "accel", HND_ENGINE_ACCEL },
    { "maxspeed", HND_MAX_VELOCITY },
    HNDX("brake", m_fBrakeDeceleration),
    HNDX("brakebias", m_fBrakeBias),
    HNDX("steering", m_fSteeringLock),
    HNDX("tractionloss", m_fTractionLoss),
    HNDX("tractionbias", m_fTractionBias),
    HNDX("suspforce", m_fSuspensionForceLevel),
    HNDX("suspdamp", m_fSuspensionDampingLevel),
    HNDX("suspdampspeed", m_fSuspensionHighSpdComDamp),
    HNDX("suspupper", m_fSuspensionUpperLimit),
    HNDX("susplower", m_fSuspensionLowerLimit),
    HNDX("suspbias", m_fSuspensionBiasBetweenFrontAndRear),
    HNDX("suspantidive", m_fSuspensionAntiDiveMultiplier),
    HNDX("collision", m_fCollisionDamageMultiplier),
    HNDX("seatoffset", m_fSeatOffsetDistance),
};
static const int HNDX_COUNT = (int)(sizeof(g_HndxFields) / sizeof(g_HndxFields[0]));

static const HndxField* HndxFind(const char* n)
{
    if (!n) return nullptr;
    for (int i = 0; i < HNDX_COUNT; i++)
        if (strcasecmp(g_HndxFields[i].name, n) == 0) return &g_HndxFields[i];
    return nullptr;
}

static void HndxFixMass(uintptr_t p)
{
    const float m = *(float*)(p + offsetof(tHandlingData, m_fMass));
    if (m > 0.0f) *(float*)(p + offsetof(tHandlingData, m_fMassRecpr)) = 1.0f / m;
}

// masinin ilk (original) handling-i: "Sifirla" ucun
static std::map<CVehicleGTA*, std::vector<uint8_t>> g_HndxOrig;

static void HndxSnap(CVehicle* v)
{
    CVehicleGTA* g = v->m_pVehicle;
    const uintptr_t p = GetCurrentHandling(g);
    if (!p) return;
    std::map<CVehicleGTA*, std::vector<uint8_t>>::iterator it = g_HndxOrig.find(g);
    if (it != g_HndxOrig.end() && it->second.size() == HND_SIZE &&
        memcmp(it->second.data(), (void*)p, 4) == 0) return;   // eyni model, snapshot kecerlidir
    g_HndxOrig[g].assign((uint8_t*)p, (uint8_t*)p + HND_SIZE);
}

static int HndxHexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool HndxParseHex(const char* s, size_t n, std::vector<uint8_t>& out)
{
    if (n % 2) return false;
    out.clear();
    for (size_t i = 0; i < n; i += 2) {
        const int a = HndxHexVal(s[i]), b = HndxHexVal(s[i + 1]);
        if (a < 0 || b < 0) return false;
        out.push_back((uint8_t)(a * 16 + b));
    }
    return true;
}

static std::string HndxPath(const char* file)
{
    return std::string(g_pszStorage ? g_pszStorage : "") + "SAMP/" + file;
}

bool HndExt_Get(CVehicle* v, const char* name, float* out)
{
    if (!v || !v->m_pVehicle || !out) return false;
    const HndxField* f = HndxFind(name);
    if (!f) return false;
    const uintptr_t p = GetCurrentHandling(v->m_pVehicle);
    if (!p) return false;
    *out = *(float*)(p + f->off);
    return true;
}

bool HndExt_Set(CVehicle* v, const char* name, float val)
{
    if (!v || !v->m_pVehicle) return false;
    if (!std::isfinite(val) || val > 1.0e6f || val < -1.0e6f) return false;
    const HndxField* f = HndxFind(name);
    if (!f) return false;
    HndxSnap(v);
    const uintptr_t p = GetOrCreateOwnHandling(v->m_pVehicle);
    if (!p) return false;
    *(float*)(p + f->off) = val;
    if (f->off == offsetof(tHandlingData, m_fMass)) HndxFixMass(p);
    return true;
}

bool HndExt_Reset(CVehicle* v)
{
    if (!v || !v->m_pVehicle) return false;
    std::map<CVehicleGTA*, std::vector<uint8_t>>::iterator it = g_HndxOrig.find(v->m_pVehicle);
    if (it == g_HndxOrig.end() || it->second.size() != HND_SIZE) return false;
    const uintptr_t p = GetOrCreateOwnHandling(v->m_pVehicle);
    if (!p) return false;
    memcpy((void*)(p + 4), it->second.data() + 4, HND_SIZE - 4);
    return true;
}

// ---- slotlar 1..5 : SAMP/handling_slots.txt ----
#define HNDX_SLOTS 5
static std::vector<uint8_t> g_HndxSlots[HNDX_SLOTS + 1];
static bool g_HndxSlotsLoaded = false;

static void HndxSlotsWrite()
{
    FILE* f = fopen(HndxPath("handling_slots.txt").c_str(), "w");
    if (!f) return;
    for (int i = 1; i <= HNDX_SLOTS; i++) {
        if (g_HndxSlots[i].empty()) continue;
        fprintf(f, "%d ", i);
        for (size_t k = 0; k < g_HndxSlots[i].size(); ++k) fprintf(f, "%02X", g_HndxSlots[i][k]);
        fprintf(f, "\n");
    }
    fclose(f);
}

static void HndxSlotsRead()
{
    if (g_HndxSlotsLoaded) return;
    g_HndxSlotsLoaded = true;
    FILE* f = fopen(HndxPath("handling_slots.txt").c_str(), "r");
    if (!f) return;
    static char line[1024];
    static char hex[900];
    while (fgets(line, sizeof(line), f)) {
        int slot = 0;
        if (sscanf(line, "%d %899s", &slot, hex) != 2) continue;
        if (slot < 1 || slot > HNDX_SLOTS) continue;
        std::vector<uint8_t> data;
        if (HndxParseHex(hex, strlen(hex), data) && data.size() == HND_SIZE) g_HndxSlots[slot] = data;
    }
    fclose(f);
}

bool HndExt_SlotUsed(int slot)
{
    if (slot < 1 || slot > HNDX_SLOTS) return false;
    HndxSlotsRead();
    return g_HndxSlots[slot].size() == HND_SIZE;
}

bool HndExt_SaveSlot(CVehicle* v, int slot)
{
    if (!v || !v->m_pVehicle || slot < 1 || slot > HNDX_SLOTS) return false;
    const uintptr_t p = GetCurrentHandling(v->m_pVehicle);
    if (!p) return false;
    HndxSlotsRead();
    g_HndxSlots[slot].assign((uint8_t*)p, (uint8_t*)p + HND_SIZE);
    HndxSlotsWrite();
    return true;
}

bool HndExt_LoadSlot(CVehicle* v, int slot)
{
    if (!v || !v->m_pVehicle || !HndExt_SlotUsed(slot)) return false;
    HndxSnap(v);
    const uintptr_t p = GetOrCreateOwnHandling(v->m_pVehicle);
    if (!p) return false;
    memcpy((void*)(p + 4), g_HndxSlots[slot].data() + 4, HND_SIZE - 4);   // ilk 4 bayt = model ID
    return true;
}

// ---- handling kodu = butun strukturun hex-i; SAMP/handling_code.txt ----
std::string HndExt_GetCode(CVehicle* v)
{
    if (!v || !v->m_pVehicle) return "";
    const uintptr_t p = GetCurrentHandling(v->m_pVehicle);
    if (!p) return "";
    std::string s = "H2";
    for (size_t i = 4; i < HND_SIZE; ++i) {
        char b[3];
        snprintf(b, sizeof(b), "%02X", ((uint8_t*)p)[i]);
        s += b;
    }
    FILE* f = fopen(HndxPath("handling_code.txt").c_str(), "w");
    if (f) { fprintf(f, "%s\n", s.c_str()); fclose(f); }
    return s;
}

static bool HndxApplyCode(CVehicle* v, const std::string& code)
{
    if (!v || !v->m_pVehicle) return false;
    if (code.size() != (size_t)(2 + 2 * (HND_SIZE - 4)) || code[0] != 'H' || code[1] != '2') return false;
    std::vector<uint8_t> data;
    if (!HndxParseHex(code.c_str() + 2, code.size() - 2, data)) return false;
    HndxSnap(v);
    const uintptr_t p = GetOrCreateOwnHandling(v->m_pVehicle);
    if (!p) return false;
    memcpy((void*)(p + 4), data.data(), data.size());
    HndxFixMass(p);
    return true;
}

bool HndExt_ImportCodeFile(CVehicle* v)
{
    FILE* f = fopen(HndxPath("handling_code.txt").c_str(), "r");
    if (!f) return false;
    char buf[800];
    const int ok = fscanf(f, "%799s", buf);
    fclose(f);
    if (ok != 1) return false;
    return HndxApplyCode(v, buf);
}

#pragma clang diagnostic pop
// ===== /HNDV2 =====
'''

MM2_BLOCK = r'''
// ===== MM2_V2: iOS stili /mm menyusu (shushe panel, yay animasiyasi, scroll siyahi) =====
bool HndExt_Get(CVehicle*, const char*, float*);
bool HndExt_Set(CVehicle*, const char*, float);
bool HndExt_Reset(CVehicle*);
bool HndExt_SlotUsed(int);
bool HndExt_SaveSlot(CVehicle*, int);
bool HndExt_LoadSlot(CVehicle*, int);
bool HndExt_ImportCodeFile(CVehicle*);
std::string HndExt_GetCode(CVehicle*);

namespace mm2
{
    static char  s_msg[112] = "";
    static float s_msgT = 0.0f;

    static void SetMsg(const std::string& m)
    {
        snprintf(s_msg, sizeof(s_msg), "%s", m.c_str());
        s_msgT = 1.0f;
    }

    static CVehicle* LocalVeh()
    {
        CPlayerPed* p = pGame ? pGame->FindPlayerPed() : nullptr;
        if (!p || !p->IsInVehicle() || !p->m_pPed) return nullptr;
        if (!pNetGame || !pNetGame->GetVehiclePool()) return nullptr;
        CVehiclePool* pool = pNetGame->GetVehiclePool();
        const VEHICLEID id = pool->FindIDFromGtaPtr(p->m_pPed->pVehicle);
        if (id == INVALID_VEHICLE_ID) return nullptr;
        CVehicle* v = pool->GetAt(id);
        return (v && v->m_pVehicle) ? v : nullptr;
    }

    struct HF { const char* key; const char* label; const char* fmt; };
    static const HF kHF[] = {
        { "mass",          "Çəki",                          "%.1f" },
        { "turnmass",      "Fırlanma kütləsi",              "%.1f" },
        { "drag",          "Hava müqaviməti",               "%.4f" },
        { "comx",          "Ağırlıq mərkəzi X",             "%.3f" },
        { "comy",          "Ağırlıq mərkəzi Y",             "%.3f" },
        { "comz",          "Ağırlıq mərkəzi Z",             "%.3f" },
        { "buoyancy",      "Üzmə qüvvəsi",                  "%.3f" },
        { "traction",      "Yol tutuş",                     "%.3f" },
        { "accel",         "Təcil",                         "%.4f" },
        { "maxspeed",      "Maksimum sürət",                "%.4f" },
        { "brake",         "Tormoz gücü",                   "%.4f" },
        { "brakebias",     "Tormoz balansı (ön/arxa)",      "%.3f" },
        { "steering",      "Sükan bucağı",                  "%.3f" },
        { "tractionloss",  "Sürüşmə itkisi",                "%.3f" },
        { "tractionbias",  "Yol tutuş balansı (ön/arxa)",   "%.3f" },
        { "suspforce",     "Süspansiyon gücü",              "%.3f" },
        { "suspdamp",      "Süspansiyon amortizatoru",      "%.3f" },
        { "suspdampspeed", "Süspansiyon sürət sönməsi",     "%.3f" },
        { "suspupper",     "Süspansiyon yuxarı hədd",       "%.3f" },
        { "susplower",     "Süspansiyon aşağı hədd",        "%.3f" },
        { "suspbias",      "Süspansiyon balansı (ön/arxa)", "%.3f" },
        { "suspantidive",  "Anti-dive (burun enməsi)",      "%.3f" },
        { "collision",     "Toqquşma zərəri",               "%.3f" },
        { "seatoffset",    "Oturacaq məsafəsi",             "%.3f" },
    };
    static const int kHFN = (int)(sizeof(kHF) / sizeof(kHF[0]));

    struct EditCaller : public Widget
    {
        std::string key;
        std::string label;
        virtual void keyboardEvent(const std::string& input) override
        {
            CVehicle* v = LocalVeh();
            if (!v || key.empty()) { SetMsg("Avtomobil tapılmadı"); return; }
            std::string t = input;
            for (size_t i = 0; i < t.size(); ++i) if (t[i] == ',') t[i] = '.';
            char* end = nullptr;
            const float f = strtof(t.c_str(), &end);
            if (end == t.c_str()) { SetMsg("Yanlış dəyər"); return; }
            if (HndExt_Set(v, key.c_str(), f)) SetMsg(label + " = " + t);
            else SetMsg("Dəyər qəbul edilmədi");
        }
    };
    static EditCaller* s_caller = nullptr;

    static float EaseOutBack(float t)
    {
        const float c1 = 1.0f, c3 = c1 + 1.0f;
        const float p = t - 1.0f;
        return 1.0f + c3 * p * p * p + c1 * p * p;
    }

    static void Hair(float x0, float x1, float y, int a = 22)
    {
        mm::s_dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), mm::Col(255, 255, 255, a), mm::S(1.0f));
    }

    // sheshe (frosted glass) panel
    static void Glass(const mm::Rc& P, float rad)
    {
        using namespace mm;
        for (int k = 0; k < 4; ++k) {
            const float e = S(3.0f + k * 6.0f);
            Fill(Rc{ P.x - e, P.y - e + S(12.0f), P.w + 2.0f * e, P.h + 2.0f * e }, Col(0, 0, 0, 30 - k * 7), rad + e);
        }
        Fill(P, Col(26, 29, 40, 218), rad);
        s_dl->AddRectFilledMultiColor(ImVec2(P.x + rad * 0.4f, P.y + S(1.0f)), ImVec2(P.x + P.w - rad * 0.4f, P.y + P.h * 0.34f),
                                      Col(255, 255, 255, 30), Col(255, 255, 255, 30), Col(255, 255, 255, 0), Col(255, 255, 255, 0));
        Stroke(P, Col(255, 255, 255, 52), rad, S(1.0f));
    }

    static void EmptyState(const mm::Rc& r, const char* title, const char* sub)
    {
        using namespace mm;
        Fill(r, Col(255, 255, 255, 12), S(18.0f));
        Dot(r.x + r.w * 0.5f, r.y + r.h * 0.5f - S(34.0f), S(26.0f), Col(255, 255, 255, 18));
        TxtC(S(26.0f), Rc{ r.x + r.w * 0.5f - S(26.0f), r.y + r.h * 0.5f - S(60.0f), S(52.0f), S(52.0f) }, Col(160, 168, 190), "!", true);
        TxtC(S(21.0f), Rc{ r.x, r.y + r.h * 0.5f + S(2.0f), r.w, S(28.0f) }, Col(236, 240, 250), title, true);
        TxtC(S(14.0f), Rc{ r.x, r.y + r.h * 0.5f + S(32.0f), r.w, S(20.0f) }, Col(130, 138, 160), sub, false);
    }

    // iOS acari
    static bool SwitchRow(int idx, const mm::Rc& row, const char* label, const char* sub, bool on, float dt, bool canInput)
    {
        using namespace mm;
        const int id = ID_TOGGLE + idx;
        const bool tapped = Tap(id, row, canInput);
        const float p = Prs(id, dt);
        s_sw[idx] = Smooth(s_sw[idx], on ? 1.0f : 0.0f, dt, 16.0f);
        const float sw = s_sw[idx];

        if (p > 0.02f) Fill(row, Col(255, 255, 255, (int)(16.0f * p)), 0.0f);
        Txt(S(19.0f), row.x + S(18.0f), row.y + S(8.0f), Col(238, 241, 250), label);
        Txt(S(13.0f), row.x + S(18.0f), row.y + S(32.0f), Col(128, 136, 158), sub);

        const Rc tr{ row.x + row.w - S(18.0f) - S(54.0f), row.y + (row.h - S(32.0f)) * 0.5f, S(54.0f), S(32.0f) };
        Fill(tr, Col((int)Lerp(72.0f, 52.0f, sw), (int)Lerp(76.0f, 199.0f, sw), (int)Lerp(92.0f, 89.0f, sw), 255), S(16.0f));
        const float kx = tr.x + S(16.0f) + sw * (tr.w - S(32.0f));
        Dot(kx, tr.y + tr.h * 0.5f + S(1.5f), S(14.0f), Col(0, 0, 0, 50));
        Dot(kx, tr.y + tr.h * 0.5f, S(13.0f) + S(1.0f) * p, Col(255, 255, 255, 255));
        return tapped;
    }

    static bool SmallBtn(int id, const mm::Rc& r, const char* label, float dt, bool canInput, bool primary)
    {
        using namespace mm;
        const bool hit = Tap(id, r, canInput);
        const float p = Prs(id, dt);
        const Rc d = Shrink(r, S(1.5f) * p);
        Fill(d, primary ? Accent(0.0f, 200 + (int)(40.0f * p)) : Col(255, 255, 255, 20 + (int)(30.0f * p)), S(12.0f));
        TxtC(S(15.0f), d, Col(244, 247, 253), label, true);
        return hit;
    }
}

void RenderHandlingDialog()
{
    using namespace mm;

    static bool          s_wasOpen = false;
    static CVehicleGTA*  s_lastVeh = nullptr;
    static bool          s_eng = false, s_lig = false, s_lock = false;
    static uint8_t       s_sig = 0;
    static float         s_t = 0.0f;
    static int           s_tab = 0;
    static float         s_pill = 0.0f, s_tabAnim = 1.0f, s_tabDir = 1.0f;
    static bool          s_outDown = false;
    static int           s_slot = 1;
    static float         s_hOff = 0.0f, s_hVel = 0.0f, s_hLastY = 0.0f, s_hDownY = 0.0f;
    static bool          s_hDrag = false, s_hMoved = false;
    static std::chrono::steady_clock::time_point s_last = std::chrono::steady_clock::now();

    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - s_last).count();
    s_last = now;
    dt = Clamp(dt, 0.0f, 0.05f);

    const bool want = g_bShowHandlingDlg;
    if (!want && s_t <= 0.0f) {
        s_wasOpen = false;
        s_hndRectValid = false;
        s_act = 0;
        s_hDrag = false;
        return;
    }

    CVehicle*    pVeh   = nullptr;
    CVehicleGTA* gtaVeh = nullptr;
    bool         noVeh  = true;
    if (want) {
        pVeh = mm2::LocalVeh();
        noVeh = (pVeh == nullptr);
        if (pVeh) {
            gtaVeh = pVeh->m_pVehicle;
            if (!s_wasOpen || gtaVeh != s_lastVeh) { s_lastVeh = gtaVeh; s_hOff = 0.0f; s_hVel = 0.0f; }
            s_eng = gtaVeh->m_nVehicleFlags.bEngineOn != 0;
            s_lig = gtaVeh->m_nVehicleFlags.bLightsOn != 0;
            { const int lk = (int)gtaVeh->m_nDoorLock; s_lock = (lk == 2 || lk == 3); }
            s_sig = VehControls::GetSignal(gtaVeh);
        }
        s_wasOpen = true;
    }

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 disp = io.DisplaySize;

    // klaviatura aciq olanda (deyer yazilir) menyu gizlenir, yuxarida kicik yazi qalir
    const bool kbOpen = pUI && pUI->keyboard() && pUI->keyboard()->visible();
    if (kbOpen && want) {
        s_hndRectValid = false;
        s_act = 0;
        s_dl = ImGui::GetForegroundDrawList();
        s_A = 1.0f;
        s_U = std::min(disp.y / 720.0f, disp.x / 880.0f);
        const Rc b{ disp.x * 0.5f - S(220.0f), S(14.0f), S(440.0f), S(44.0f) };
        Fill(b, Col(26, 29, 40, 236), S(22.0f));
        Stroke(b, Col(255, 255, 255, 46), S(22.0f), S(1.0f));
        TxtC(S(16.0f), b, Col(238, 241, 250), mm2::s_caller ? (mm2::s_caller->label + " — yeni dəyəri yaz").c_str() : "Yeni dəyəri yaz", true);
        return;
    }

    s_t = Clamp(s_t + (want ? dt / 0.38f : -dt / 0.18f), 0.0f, 1.0f);
    const float open = want ? mm2::EaseOutBack(s_t) : EaseOutCubic(s_t);
    s_A = Clamp(s_t * 2.2f, 0.0f, 1.0f);
    s_tabAnim = Clamp(s_tabAnim + dt / 0.26f, 0.0f, 1.0f);
    mm2::s_msgT = Clamp(mm2::s_msgT - dt / 3.5f, 0.0f, 1.0f);

    const float W0 = 880.0f, H0 = 560.0f;
    float u0 = std::min(disp.y / 720.0f, std::min(disp.x * 0.96f / W0, disp.y * 0.96f / H0));
    u0 = std::min(u0, std::min(disp.x * 0.98f / W0, disp.y * 0.98f / H0));
    s_U = u0 * (0.90f + 0.10f * open);

    const Rc P{ disp.x * 0.5f - W0 * s_U * 0.5f, disp.y * 0.5f - H0 * s_U * 0.5f, W0 * s_U, H0 * s_U };
    const bool canInput = want && s_t > 0.8f;
    const MMTouch& tch = g_mmTouch;

    s_hndRectValid = (s_t > 0.05f);
    s_hndPos  = ImVec2(P.x, P.y);
    s_hndSize = ImVec2(P.w, P.h);
    s_dl = ImGui::GetForegroundDrawList();

    // fonu qaraldan perde
    s_dl->AddRectFilled(ImVec2(0.0f, 0.0f), disp, IM_COL32(0, 0, 0, (int)(115.0f * Clamp(s_t, 0.0f, 1.0f))));

    // paneldan kenara toxunus = bagla
    if (canInput) {
        if (tch.pressed && s_act == 0 && !In(P, tch.x, tch.y)) s_outDown = true;
        if (tch.released) {
            if (s_outDown && !In(P, tch.x, tch.y)) g_bShowHandlingDlg = false;
            s_outDown = false;
        }
    }

    mm2::Glass(P, S(28.0f));

    // grabber + basliq + bagla
    Fill(Rc{ P.x + P.w * 0.5f - S(22.0f), P.y + S(9.0f), S(44.0f), S(5.0f) }, Col(255, 255, 255, 54), S(2.5f));
    TxtC(S(22.0f), Rc{ P.x, P.y + S(18.0f), P.w, S(34.0f) }, Col(244, 247, 253), "Maşın menyusu", true);
    {
        const Rc cl{ P.x + P.w - S(54.0f), P.y + S(16.0f), S(36.0f), S(36.0f) };
        if (Tap(ID_CLOSE, cl, canInput)) g_bShowHandlingDlg = false;
        const float p = Prs(ID_CLOSE, dt);
        Dot(cl.x + cl.w * 0.5f, cl.y + cl.h * 0.5f, S(15.0f) - S(1.5f) * p, Col(255, 255, 255, 26 + (int)(30.0f * p)));
        const float cx = cl.x + cl.w * 0.5f, cy = cl.y + cl.h * 0.5f, k = S(5.0f);
        s_dl->AddLine(ImVec2(cx - k, cy - k), ImVec2(cx + k, cy + k), Col(236, 240, 250, 230), S(2.0f));
        s_dl->AddLine(ImVec2(cx - k, cy + k), ImVec2(cx + k, cy - k), Col(236, 240, 250, 230), S(2.0f));
    }

    // segmentli idareetme (iOS)
    static const char* kTabs[4] = { "Maşın", "Handling", "Far", "Spidometr" };
    {
        const Rc seg{ P.x + S(24.0f), P.y + S(66.0f), P.w - S(48.0f), S(40.0f) };
        Fill(seg, Col(255, 255, 255, 18), S(13.0f));
        const float tw = seg.w / 4.0f;
        s_pill = Smooth(s_pill, (float)s_tab, dt, 16.0f);
        Fill(Rc{ seg.x + S(3.0f) + s_pill * tw, seg.y + S(3.0f), tw - S(6.0f), seg.h - S(6.0f) }, Col(255, 255, 255, 50), S(10.5f));
        for (int i = 0; i < 4; ++i) {
            const Rc ti{ seg.x + i * tw, seg.y, tw, seg.h };
            if (Tap(ID_NAV0 + i, ti, canInput) && s_tab != i) {
                s_tabDir = (i > s_tab) ? 1.0f : -1.0f;
                s_tab = i;
                s_tabAnim = 0.0f;
            }
            Prs(ID_NAV0 + i, dt);
            const bool on = (s_tab == i);
            TxtC(S(16.0f), ti, on ? Col(248, 250, 255) : Col(150, 158, 180), kTabs[i], on);
        }
    }

    // ---- sehife mezmunu ----
    const float saveA = s_A;
    const float pe = EaseOutCubic(s_tabAnim);
    s_A = saveA * pe;
    const float cx0 = P.x + S(24.0f) + (1.0f - pe) * S(26.0f) * s_tabDir;
    const float cw  = P.w - S(48.0f);
    const float y0  = P.y + S(124.0f);
    const bool  inputPage = canInput && s_tabAnim > 0.6f;

    if (s_tab == 0)                                         // MASIN
    {
        if (noVeh) {
            mm2::EmptyState(Rc{ cx0, y0, cw, S(300.0f) }, "Avtomobildə deyilsiniz", "Bu bölmə yalnız maşının içində işləyir");
        } else {
            const Rc grp{ cx0, y0, cw, S(168.0f) };
            Fill(grp, Col(255, 255, 255, 15), S(18.0f));
            Stroke(grp, Col(255, 255, 255, 22), S(18.0f), S(1.0f));
            const char* labs[3] = { "Mühərrik", "Fara", "Qapılar" };
            const char* subs[3] = { s_eng ? "İşləyir" : "Söndürülüb", s_lig ? "Yanır" : "Sönülü", s_lock ? "Kilidli" : "Açıq" };
            const bool  st[3]   = { s_eng, s_lig, s_lock };
            for (int i = 0; i < 3; ++i) {
                const Rc row{ cx0, y0 + S(56.0f) * i, cw, S(56.0f) };
                if (i > 0) mm2::Hair(row.x + S(18.0f), row.x + row.w, row.y);
                if (mm2::SwitchRow(i, row, labs[i], subs[i], st[i], dt, inputPage) && pVeh) {
                    if (i == 0)      pVeh->ApplyEngineState(s_eng ? 0 : 1);
                    else if (i == 1) pVeh->ApplyLightState(s_lig ? 0 : 1);
                    else             pVeh->SetDoorState(s_lock ? 0 : 1);
                }
            }
            SignalBlock(Rc{ cx0, y0 + S(186.0f), cw, S(124.0f) }, s_sig, gtaVeh, dt, inputPage);
        }
    }
    else if (s_tab == 1)                                    // HANDLING
    {
        if (noVeh) {
            mm2::EmptyState(Rc{ cx0, y0, cw, S(300.0f) }, "Avtomobildə deyilsiniz", "Handling yalnız maşının içində dəyişdirilir");
        } else {
            // slot secimi
            for (int i = 0; i < 5; ++i) {
                const Rc c{ cx0 + S(52.0f) * i, y0, S(46.0f), S(40.0f) };
                if (Tap(80 + i, c, inputPage)) s_slot = i + 1;
                const bool on = (s_slot == i + 1);
                Fill(c, on ? Accent(0.0f, 215) : Col(255, 255, 255, 18), S(12.0f));
                char nb[4];
                snprintf(nb, sizeof(nb), "%d", i + 1);
                TxtC(S(18.0f), c, on ? Col(255, 255, 255) : Col(190, 198, 216), nb, true);
                if (HndExt_SlotUsed(i + 1)) Dot(c.x + c.w - S(8.0f), c.y + S(8.0f), S(3.2f), Col(80, 220, 140, 255));
            }
            // emeller
            static const char* kAct[5] = { "Yaz", "Yüklə", "Kod", "Fayl", "Sıfırla" };
            const float bx = cx0 + S(272.0f);
            const float bw = (cw - S(272.0f) - S(6.0f) * 4.0f) / 5.0f;
            for (int k = 0; k < 5; ++k) {
                const Rc b{ bx + k * (bw + S(6.0f)), y0, bw, S(40.0f) };
                if (mm2::SmallBtn(85 + k, b, kAct[k], dt, inputPage, k == 0) && pVeh) {
                    if (k == 0)      mm2::SetMsg(HndExt_SaveSlot(pVeh, s_slot) ? "Slot yadda saxlandı" : "Yadda saxlanmadı");
                    else if (k == 1) mm2::SetMsg(HndExt_LoadSlot(pVeh, s_slot) ? "Slot yükləndi" : "Slot boşdur");
                    else if (k == 2) {
                        const std::string code = HndExt_GetCode(pVeh);
                        if (pUI && pUI->chat() && !code.empty()) {
                            pUI->chat()->addDebugMessage("{00FF00}[Handling]: kod SAMP/handling_code.txt faylına yazıldı:");
                            pUI->chat()->addDebugMessage("{B9C9BF}%s", code.c_str());
                        }
                        mm2::SetMsg(code.empty() ? "Kod alınmadı" : "Kod fayla yazıldı və çata çıxdı");
                    }
                    else if (k == 3) mm2::SetMsg(HndExt_ImportCodeFile(pVeh) ? "Koddan yükləndi" : "handling_code.txt tapılmadı / kod yanlışdır");
                    else             mm2::SetMsg(HndExt_Reset(pVeh) ? "Standarta qaytarıldı" : "Dəyişiklik yoxdur");
                }
            }
            if (mm2::s_msgT > 0.01f)
                Txt(S(13.0f), cx0, y0 + S(48.0f), Col(120, 215, 160, (int)(255.0f * mm2::s_msgT)), mm2::s_msg);
            else
                Txt(S(13.0f), cx0, y0 + S(48.0f), Col(128, 136, 158), "Sətrə toxun və yeni dəyəri yaz. Sürüşdürərək bütün sahələri gör.");

            // sahe siyahisi (scroll)
            const float rowH = S(46.0f);
            const Rc lr{ cx0, y0 + S(74.0f), cw, P.y + P.h - S(18.0f) - (y0 + S(74.0f)) };
            const float contentH = rowH * mm2::kHFN;
            const float maxOff = std::max(0.0f, contentH - lr.h);

            if (inputPage) {
                if (tch.pressed && s_act == 0 && In(lr, tch.x, tch.y)) {
                    s_act = 95; s_hDrag = true; s_hMoved = false; s_hLastY = tch.y; s_hDownY = tch.y; s_hVel = 0.0f;
                }
                if (s_hDrag && s_act == 95) {
                    if (tch.down) {
                        const float dy = tch.y - s_hLastY;
                        s_hLastY = tch.y;
                        if (fabsf(tch.y - s_hDownY) > S(8.0f)) s_hMoved = true;
                        if (s_hMoved) {
                            const float over = (s_hOff < 0.0f) ? -s_hOff : (s_hOff > maxOff ? s_hOff - maxOff : 0.0f);
                            const float k = 1.0f / (1.0f + over / S(60.0f));
                            s_hOff -= dy * k;
                            s_hVel = Lerp(s_hVel, -dy / std::max(dt, 0.001f), 0.35f);
                        }
                    }
                    if (tch.released) {
                        if (!s_hMoved && In(lr, tch.x, tch.y)) {
                            const int row = (int)((tch.y - lr.y + s_hOff) / rowH);
                            if (row >= 0 && row < mm2::kHFN && pUI && pUI->keyboard()) {
                                if (!mm2::s_caller) mm2::s_caller = new mm2::EditCaller();
                                mm2::s_caller->key = mm2::kHF[row].key;
                                mm2::s_caller->label = mm2::kHF[row].label;
                                pUI->keyboard()->show(mm2::s_caller);
                                float cur = 0.0f;
                                if (HndExt_Get(pVeh, mm2::kHF[row].key, &cur)) {
                                    char vb[32];
                                    snprintf(vb, sizeof(vb), "%.4f", cur);
                                    pUI->keyboard()->input()->setInputString(vb);
                                }
                            }
                            s_hVel = 0.0f;
                        }
                        s_hDrag = false;
                    }
                }
            }
            if (!(s_hDrag && s_act == 95)) {
                if (s_hOff < 0.0f || s_hOff > maxOff) {
                    s_hOff = Smooth(s_hOff, Clamp(s_hOff, 0.0f, maxOff), dt, 14.0f);
                    s_hVel = 0.0f;
                } else {
                    s_hOff += s_hVel * dt;
                    s_hVel *= expf(-dt * 3.5f);
                    if (fabsf(s_hVel) < 5.0f) s_hVel = 0.0f;
                }
            }

            Fill(lr, Col(255, 255, 255, 15), S(18.0f));
            Stroke(lr, Col(255, 255, 255, 22), S(18.0f), S(1.0f));
            s_dl->PushClipRect(ImVec2(lr.x, lr.y + S(1.0f)), ImVec2(lr.x + lr.w, lr.y + lr.h - S(1.0f)), true);
            for (int r = 0; r < mm2::kHFN; ++r) {
                const float ry = lr.y - s_hOff + rowH * r;
                if (ry + rowH < lr.y || ry > lr.y + lr.h) continue;
                if (s_hDrag && !s_hMoved && s_act == 95 && tch.y >= ry && tch.y < ry + rowH)
                    Fill(Rc{ lr.x, ry, lr.w, rowH }, Col(255, 255, 255, 16), 0.0f);
                if (r > 0) mm2::Hair(lr.x + S(18.0f), lr.x + lr.w, ry);
                Txt(S(17.0f), lr.x + S(18.0f), ry + (rowH - S(17.0f)) * 0.5f, Col(228, 232, 244), mm2::kHF[r].label);
                float cur = 0.0f;
                char vb[40] = "-";
                if (HndExt_Get(pVeh, mm2::kHF[r].key, &cur)) snprintf(vb, sizeof(vb), mm2::kHF[r].fmt, cur);
                Txt(S(17.0f), lr.x + lr.w - S(40.0f) - TW(S(17.0f), vb), ry + (rowH - S(17.0f)) * 0.5f, Accent(0.0f), vb);
                Txt(S(18.0f), lr.x + lr.w - S(26.0f), ry + (rowH - S(18.0f)) * 0.5f, Col(110, 118, 140), ">");
            }
            s_dl->PopClipRect();
            if (maxOff > 1.0f) {
                const float th = std::max(S(26.0f), lr.h * (lr.h / contentH));
                const float ty = lr.y + S(4.0f) + (lr.h - S(8.0f) - th) * Clamp(s_hOff / maxOff, 0.0f, 1.0f);
                Fill(Rc{ lr.x + lr.w - S(6.0f), ty, S(3.0f), th }, Col(255, 255, 255, 60), S(1.5f));
            }
        }
    }
    else if (s_tab == 2)                                    // FAR
    {
        if (noVeh) {
            mm2::EmptyState(Rc{ cx0, y0, cw, S(300.0f) }, "Avtomobildə deyilsiniz", "Far rəngi yalnız maşının içində dəyişdirilir");
        } else {
            static CVehicleGTA* s_hlVeh = nullptr;
            static float s_hl[3] = { 255.0f, 255.0f, 255.0f };
            if (gtaVeh != s_hlVeh) {
                s_hlVeh = gtaVeh;
                uint8_t r8 = 255, g8 = 255, b8 = 255;
                if (gtaVeh && CCoronas::GetHeadlightRGB(gtaVeh, r8, g8, b8)) { s_hl[0] = r8; s_hl[1] = g8; s_hl[2] = b8; }
                else { s_hl[0] = s_hl[1] = s_hl[2] = 255.0f; }
            }
            static const char* kRGB[3] = { "Qırmızı (R)", "Yaşıl (G)", "Mavi (B)" };
            bool hlChanged = false;
            for (int i = 0; i < 3; ++i) {
                const Rc row{ cx0, y0 + S(72.0f) * (float)i - S(4.0f) + S(4.0f), cw, S(66.0f) };
                if (SliderRow(i, row, kRGB[i], &s_hl[i], 0.0f, 255.0f, dt, inputPage, "", 5.0f)) hlChanged = true;
            }
            const Rc pvr{ cx0, y0 + S(224.0f), S(150.0f), S(44.0f) };
            Fill(pvr, Col((int)s_hl[0], (int)s_hl[1], (int)s_hl[2], 255), S(12.0f));
            Stroke(pvr, Col(255, 255, 255, 60), S(12.0f), S(1.2f));
            char hx[24];
            snprintf(hx, sizeof(hx), "#%02X%02X%02X", (int)s_hl[0], (int)s_hl[1], (int)s_hl[2]);
            Txt(S(19.0f), cx0 + S(166.0f), y0 + S(234.0f), Col(228, 232, 244), hx);
            if (mm2::SmallBtn(90, Rc{ cx0 + S(300.0f), y0 + S(224.0f), S(150.0f), S(44.0f) }, "Standart", dt, inputPage, false) && gtaVeh) {
                CCoronas::ClearHeadlightRGB(gtaVeh);
                s_hl[0] = s_hl[1] = s_hl[2] = 255.0f;
            }
            for (int i = 0; i < VEHTUNER_COLOR_COUNT; ++i) {
                uint8_t r, g, b;
                VehTuner::GetPaletteColor(i, r, g, b);
                const int col = i % 8, rowi = i / 8;
                const float cxs = cx0 + S(24.0f) + (float)col * S(58.0f);
                const float cys = y0 + S(320.0f) + (float)rowi * S(54.0f);
                const Rc sq{ cxs - S(24.0f), cys - S(24.0f), S(48.0f), S(48.0f) };
                const int sid = ID_SWATCH + std::min(i, 23);
                if (Tap(sid, sq, inputPage) && gtaVeh) { s_hl[0] = r; s_hl[1] = g; s_hl[2] = b; hlChanged = true; }
                const float p = Prs(sid, dt);
                Dot(cxs, cys, S(20.0f) - S(2.0f) * p, Col(r, g, b, 255));
            }
            if (hlChanged && gtaVeh) CCoronas::SetHeadlightRGB(gtaVeh, (uint8_t)s_hl[0], (uint8_t)s_hl[1], (uint8_t)s_hl[2]);
        }
    }
    else                                                    // SPIDOMETR
    {
        MM_SpeedoPage(cx0, cw, P, dt, inputPage);
    }
    s_A = saveA;

    if (!g_mmTouch.down) s_act = 0;
}
'''



OLD_MARKERS = ["PROFILE_PATCH11", "TG_PATCH10", "EMOJI_PATCH11", "HNDEXT_PATCH8"]

def rd(p):
    with open(p, encoding="utf-8") as f: return f.read()

def sub1(pat, repl, s, flags=0):
    new, n = re.subn(pat, (lambda m: repl) if isinstance(repl, str) else repl, s, count=1, flags=flags)
    if n != 1: raise ValueError(pat[:70])
    return new

def remove_block(s, header_re):
    """if (...) { ... } blokunu mötərizə sayı ilə silir (string/komment içindəkilərə fikir vermir)."""
    m = re.search(header_re, s)
    if not m: raise ValueError(header_re)
    i = s.index("{", m.end() - 1 if s[m.end() - 1] == "{" else m.end())
    depth, k, n = 0, i, len(s)
    while k < n:
        c = s[k]
        if c == '"':
            k += 1
            while k < n and s[k] != '"':
                if s[k] == "\\": k += 1
                k += 1
        elif c == "'":
            k += 1
            while k < n and s[k] != "'":
                if s[k] == "\\": k += 1
                k += 1
        elif s.startswith("//", k):
            k = s.index("\n", k) if "\n" in s[k:] else n
            continue
        elif s.startswith("/*", k):
            k = s.index("*/", k) + 1
        elif c == "{": depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0: break
        k += 1
    start = s.rfind("\n", 0, m.start()) + 1
    end = k + 1
    while end < n and s[end] in " \t": end += 1
    if end < n and s[end] == "\n": end += 1
    if end < n and s[end] == "\n": end += 1
    return s[:start] + s[end:]

def step(name, path, marker, fn):
    if not os.path.exists(path):
        print("[YOX]   %s: %s tapilmadi" % (name, os.path.relpath(path, ROOT))); return
    s = rd(path)
    if marker and marker in s:
        print("[KECDI] %s: artiq tetbiq olunub" % name); return
    try:
        n = fn(s)
    except ValueError as e:
        print("[KECDI] %s: anker tapilmadi -> %s (fayla toxunulmadi)" % (name, e)); return
    if not os.path.exists(path + ".bakv2"): shutil.copy(path, path + ".bakv2")
    with open(path, "w", encoding="utf-8") as f: f.write(n)
    print("[OK]    %s" % name)

# ------------------------------------------------------------------ 0. baza yoxlamasi
def check_base():
    bad = []
    for d, _, fs in os.walk(os.path.join(ROOT, "app/src/main")):
        for f in fs:
            if f.endswith((".cpp", ".h", ".java")):
                try: t = rd(os.path.join(d, f))
                except Exception: continue
                for m in OLD_MARKERS:
                    if m in t: bad.append("%s (%s)" % (os.path.relpath(os.path.join(d, f), ROOT), m))
    if bad:
        print("DAYAN: kohne patch izleri tapildi:\n  " + "\n  ".join(bad[:12]))
        print("\nEvvelce temiz bazaya qayit (bir defelik):\n  git checkout 27d7d40 -- app\n  git commit -am 'temiz baza'\nSonra bu skripti yeniden isle.")
        raise SystemExit(1)

# ------------------------------------------------------------------ 1. vehicle.cpp
def p_vehicle(s):
    if "GetOrCreateOwnHandling" not in s: raise ValueError("GetOrCreateOwnHandling")
    return s.rstrip("\n") + "\n" + HND_BLOCK

# ------------------------------------------------------------------ 2. gui.cpp
def p_gui(s):
    s = sub1(r'void RenderHandlingDialog\(\)\s*\{',
             'void RenderHandlingDialog();   // MM2_V2 (yeni, faylin sonunda)\nstatic void RenderHandlingDialog_Old()\n{', s)
    return s.rstrip("\n") + "\n" + MM2_BLOCK

# ------------------------------------------------------------------ 3. chat.cpp
def p_chat(s):
    s = remove_block(s, r'if \(cmd == "/cam"\) \{')
    s = remove_block(s, r'if \(cmd == "/dump"\)\s*\{')
    s = remove_block(s, r'if \(cmd == "/nomre"\)\s*\{')
    return s + "\n// MM2_V2: /cam, /dump, /nomre silindi\n"

# ------------------------------------------------------------------ 4. hooks.cpp
def p_hooks(s):
    return sub1(r'sprintf\(path, "%sSAMP/handling\.cfg", g_pszStorage\);',
                'sprintf(path, "%sSAMP/handling_custom.cfg", g_pszStorage);   // MM2_V2: launcherden secilen fayl\n'
                '        { FILE* hcf = fopen(path, "rb"); if (hcf) fclose(hcf); else sprintf(path, "%sSAMP/handling.cfg", g_pszStorage); }', s)

# ------------------------------------------------------------------ 5. logger
def p_settings(s):
    s = sub1(r'(extern CGame \*pGame;[^\n]*\n)', lambda m: m.group(1) + CRASH_BLOCK, s)
    s = sub1(r'(\tFLog\("Settings loaded\."\);[^\n]*\n)', lambda m: m.group(1) +
             '\n\t{   // CRASHLOG\n\t\tchar cpath[0x100];\n\t\tsnprintf(cpath, sizeof(cpath), "%sSAMP/crash.log", g_pszStorage);\n\t\tcrashlog::Install(cpath);\n\t}\n', s)
    return s

def p_game(s):
    s = sub1(r'(void CGame::Process\(\)\s*\{\s*if\s*\(bIsGameExiting\)\s*return;)',
             lambda m: m.group(1) + '\n    PerfLog_Frame();   // CRASHLOG', s)
    return s.replace('void CGame::Process()', 'void PerfLog_Frame();   // settings.cpp\nvoid CGame::Process()', 1)

# ------------------------------------------------------------------ 6. Java
def p_settings_java(s):
    s = sub1(r'(import android\.widget\.ToggleButton;[^\n]*\n)', lambda m: m.group(1) +
             'import android.net.Uri;   // MM2_V2\nimport android.widget.Toast;\nimport androidx.activity.result.ActivityResultCallback;\n'
             'import androidx.activity.result.ActivityResultLauncher;\nimport androidx.activity.result.contract.ActivityResultContracts;\n'
             'import java.io.FileOutputStream;\nimport java.io.InputStream;\n', s)
    methods = '''
    // MM2_V2: handling.cfg secimi -> SAMP/handling_custom.cfg (oyun acilanda bunu yukleyir)
    private final ActivityResultLauncher<String[]> hndPicker = registerForActivityResult(
            new ActivityResultContracts.OpenDocument(), new ActivityResultCallback<Uri>() {
                @Override
                public void onActivityResult(Uri uri) {
                    if (uri != null) copyHandling(uri);
                }
            });

    private File hndTarget() {
        File dir = new File(requireActivity().getExternalFilesDir(null), "SAMP");
        if (!dir.exists()) dir.mkdirs();
        return new File(dir, "handling_custom.cfg");
    }

    private void copyHandling(Uri uri) {
        try {
            InputStream in = requireActivity().getContentResolver().openInputStream(uri);
            if (in == null) throw new IOException("fayl açıla bilmədi");
            FileOutputStream os = new FileOutputStream(hndTarget());
            byte[] buf = new byte[8192];
            int n;
            long total = 0;
            while ((n = in.read(buf)) > 0) { os.write(buf, 0, n); total += n; }
            os.close();
            in.close();
            Toast.makeText(getActivity(), "handling.cfg yükləndi (" + total + " bayt). Oyunu yenidən başlat.", Toast.LENGTH_LONG).show();
        } catch (Exception e) {
            Toast.makeText(getActivity(), "Xəta: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

'''
    s = sub1(r'(\n    @Override\n    public View onCreateView\()', lambda m: methods + m.group(1), s)
    s = sub1(r'(mFPSText = view\.findViewById\(R\.id\.fps_count\);[^\n]*\n)', lambda m: m.group(1) +
             '''
        // MM2_V2: handling.cfg düymələri
        Button hndPick = view.findViewById(R.id.handling_pick);
        Button hndReset = view.findViewById(R.id.handling_reset);
        hndPick.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                hndPicker.launch(new String[]{"*/*"});
            }
        });
        hndReset.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                boolean ok = hndTarget().delete();
                Toast.makeText(getActivity(), ok ? "Standart handling.cfg qaytarıldı. Oyunu yenidən başlat." : "Seçilmiş fayl yoxdur", Toast.LENGTH_LONG).show();
            }
        });
''', s)
    return s

LAYOUT_ADD = '''
    <TextView
        android:id="@+id/handling_label"
        android:layout_width="wrap_content"
        android:layout_height="wrap_content"
        android:fontFamily="@font/montserrat_bold"
        android:text="Handling.cfg"
        android:textColor="@color/white"
        android:textSize="@dimen/_12sdp"
        app:layout_constraintBottom_toBottomOf="parent"
        app:layout_constraintEnd_toEndOf="parent"
        app:layout_constraintHorizontal_bias="0.145"
        app:layout_constraintStart_toStartOf="parent"
        app:layout_constraintTop_toBottomOf="@+id/settings_nickname"
        app:layout_constraintVertical_bias="0.715" />

    <Button
        android:id="@+id/handling_pick"
        android:layout_width="@dimen/_90sdp"
        android:layout_height="@dimen/_25sdp"
        android:background="@drawable/ic_nickname_bg"
        android:fontFamily="@font/montserrat_bold"
        android:text="Fayl seç"
        android:textColor="@color/white"
        android:textSize="@dimen/_10sdp"
        app:layout_constraintBottom_toBottomOf="parent"
        app:layout_constraintEnd_toEndOf="parent"
        app:layout_constraintHorizontal_bias="0.68"
        app:layout_constraintStart_toStartOf="parent"
        app:layout_constraintTop_toBottomOf="@+id/settings_nickname"
        app:layout_constraintVertical_bias="0.715" />

    <Button
        android:id="@+id/handling_reset"
        android:layout_width="@dimen/_60sdp"
        android:layout_height="@dimen/_25sdp"
        android:background="@drawable/ic_nickname_bg"
        android:fontFamily="@font/montserrat_bold"
        android:text="Sıfırla"
        android:textColor="@color/white"
        android:textSize="@dimen/_10sdp"
        app:layout_constraintBottom_toBottomOf="parent"
        app:layout_constraintEnd_toEndOf="parent"
        app:layout_constraintHorizontal_bias="0.97"
        app:layout_constraintStart_toStartOf="parent"
        app:layout_constraintTop_toBottomOf="@+id/settings_nickname"
        app:layout_constraintVertical_bias="0.715" />

'''
def p_layout(s):
    i = s.rfind("</androidx.constraintlayout.widget.ConstraintLayout>")
    if i < 0: raise ValueError("ConstraintLayout sonu")
    return s[:i] + "    <!-- MM2_V2 -->" + LAYOUT_ADD + s[i:]

def p_splash(s):
    m = re.search(r'setContentView\(R\.layout\.activity_splash\);[^\n]*\n', s)
    if not m: raise ValueError("setContentView(activity_splash)")
    return s[:m.end()] + SPLASH_ADD.replace("SPLASH_ANIM_PATCH8", "MM2_V2") + s[m.end():]

def p_lobby(s):
    if re.search(r'void onPause\(\)|void onDestroy\(\)', s): raise ValueError("onPause/onDestroy artiq var")
    m = re.search(r'([ \t]*@Override\s*\n[ \t]*protected void onResume\(\) \{\s*\n[ \t]*super\.onResume\(\);)', s)
    if not m: raise ValueError("onResume")
    out = s[:m.start()] + MUSIC.replace("LOBBY_MUSIC_PATCH8", "MM2_V2") + m.group(1) + "\n        startBgMusic();" + s[m.end():]
    raw = os.path.join(RES, "raw")
    os.makedirs(raw, exist_ok=True)
    with open(os.path.join(raw, "README_music.txt"), "w", encoding="utf-8") as f:
        f.write("Bura 3 mahni qoy: bg_music1.mp3, bg_music2.mp3, bg_music3.mp3 (kicik herf, bosluqsuz).\nOlmayan slot kecilir.\n")
    return out

# ------------------------------------------------------------------ 7. nametag bayragi
def p_tags(s):
    pat = re.compile(r'^[ \t]*pos\.x -= renderer->calculateTextSize\(szNick, UISettings::fontSize\(\) / 2\)\.x / 2;[ \t]*\n'
                     r'[ \t]*renderer->drawText\(pos, __builtin_bswap32\(dwColor \| \(0x000000FF\)\), std::string\(szNick\), true, UISettings::fontSize\(\) / 2\);[ \t]*\n', re.M)
    m = pat.search(s)
    if not m: raise ValueError("nametag drawText")
    s = s[:m.start()] + "\t" + FLAG_NEWNAME + s[m.end():]
    d = re.search(r'^void CPlayerTags::Draw\(', s, re.M)
    if not d: raise ValueError("CPlayerTags::Draw")
    return s[:d.start()] + FLAG_HELPER + s[d.start():]

if __name__ == "__main__":
    check_base()
    step("vehicle.cpp: handling redaktoru (sahe, slot, kod)", os.path.join(CPP, "game/vehicle.cpp"), "HNDV2", p_vehicle)
    step("gui.cpp: yeni /mm menyusu", os.path.join(CPP, "gui/gui.cpp"), "MM2_V2", p_gui)
    step("chat.cpp: /cam, /dump, /nomre silindi", os.path.join(CPP, "gui/samp_widgets/chat.cpp"), "MM2_V2", p_chat)
    step("hooks.cpp: handling_custom.cfg", os.path.join(CPP, "game/hooks.cpp"), "MM2_V2", p_hooks)
    step("settings.cpp: crash.log / perf.log", os.path.join(CPP, "settings.cpp"), "CRASHLOG", p_settings)
    step("game.cpp: kadr olcusu", os.path.join(CPP, "game/game.cpp"), "CRASHLOG", p_game)
    step("SettingsFragment.java: handling.cfg sec", os.path.join(JAVA, "fragments/SettingsFragment.java"), "MM2_V2", p_settings_java)
    step("fragment_settings.xml: handling.cfg duymeleri", os.path.join(RES, "layout/fragment_settings.xml"), "MM2_V2", p_layout)
    step("SplashActivity.java: loading animasiyasi", os.path.join(JAVA, "SplashActivity.java"), "MM2_V2", p_splash)
    step("LobbyActivity.java: arxa fon musiqisi", os.path.join(JAVA, "LobbyActivity.java"), "MM2_V2", p_lobby)
    step("playertags.cpp: AZ bayragi", os.path.join(CPP, "playertags.cpp"), "AZFLAG_PATCH9", p_tags)
    print("\nBitdi. Geri qaytarmaq: hər faylın yanında *.bakv2")
    print("Oyunda: crash.log / perf.log -> settings.ini ilə eyni SAMP qovluğu")
