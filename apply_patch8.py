#!/usr/bin/env python3
# apply_patch8.py - Merhele 2 (handling sistemi) + Merhele 7 (loading animasiyasi, launcher musiqisi)
# Istifade: layihenin kokunde:  python apply_patch8.py
import os, re, shutil, sys

ROOT = os.getcwd()
VEH  = "app/src/main/cpp/samp/game/vehicle.cpp"
CHAT = "app/src/main/cpp/samp/gui/samp_widgets/chat.cpp"
SPL  = "app/src/main/java/com/samp/mobile/launcher/SplashActivity.java"
LOB  = "app/src/main/java/com/samp/mobile/launcher/LobbyActivity.java"
RAW  = "app/src/main/res/raw"

def rd(p):
    with open(os.path.join(ROOT, p), encoding="utf-8") as f: return f.read()

def wr(p, s):
    full = os.path.join(ROOT, p)
    if not os.path.exists(full + ".bak8"): shutil.copy(full, full + ".bak8")
    with open(full, "w", encoding="utf-8") as f: f.write(s)

def run(name, fn):
    try:
        print(("[OK]    " if fn() else "[KECDI] ") + name)
    except FileNotFoundError as e:
        print("[YOX]   %s: %s tapilmadi" % (name, e.filename))

# ====================================================================
# C++ : vehicle.cpp - handling sahe/slot/kod/cfg funksiyalari
# ====================================================================
VEH_BLOCK = r'''

// ===== HNDEXT_PATCH8: handling sahe sistemi / slotlar / kod / cfg =====
#include <vector>
#include <string>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include "tHandlingData.h"

extern char* g_pszStorage;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"

struct HndExtField { const char* name; size_t off; };
#define HNDX(n, m) { n, offsetof(tHandlingData, m) }
static const HndExtField g_HndExtFields[] = {
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
static const int HNDX_COUNT = (int)(sizeof(g_HndExtFields) / sizeof(g_HndExtFields[0]));

static const HndExtField* HndxFind(const char* n)
{
    if (!n) return nullptr;
    for (int i = 0; i < HNDX_COUNT; i++)
        if (strcasecmp(g_HndExtFields[i].name, n) == 0) return &g_HndExtFields[i];
    return nullptr;
}

static void HndxFixMass(uintptr_t p)
{
    float m = *(float*)(p + offsetof(tHandlingData, m_fMass));
    if (m > 0.0f) *(float*)(p + offsetof(tHandlingData, m_fMassRecpr)) = 1.0f / m;
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
        int a = HndxHexVal(s[i]), b = HndxHexVal(s[i + 1]);
        if (a < 0 || b < 0) return false;
        out.push_back((uint8_t)(a * 16 + b));
    }
    return true;
}

static std::string HndxStorage(const char* file)
{
    return std::string(g_pszStorage ? g_pszStorage : "") + "SAMP/" + file;
}

std::string HndExt_FieldList()
{
    std::string s;
    for (int i = 0; i < HNDX_COUNT; i++) { if (i) s += ", "; s += g_HndExtFields[i].name; }
    return s;
}

bool HndExt_Set(CVehicle* v, const char* name, float val)
{
    if (!v || !v->m_pVehicle) return false;
    const HndExtField* f = HndxFind(name);
    if (!f) return false;
    uintptr_t p = GetOrCreateOwnHandling(v->m_pVehicle);
    if (!p) return false;
    *(float*)(p + f->off) = val;
    if (f->off == offsetof(tHandlingData, m_fMass)) HndxFixMass(p);
    return true;
}

bool HndExt_Get(CVehicle* v, const char* name, float* out)
{
    if (!v || !v->m_pVehicle || !out) return false;
    const HndExtField* f = HndxFind(name);
    if (!f) return false;
    uintptr_t p = GetCurrentHandling(v->m_pVehicle);
    if (!p) return false;
    *out = *(float*)(p + f->off);
    return true;
}

std::vector<std::string> HndExt_DumpLines(CVehicle* v)
{
    std::vector<std::string> lines;
    if (!v || !v->m_pVehicle) return lines;
    uintptr_t p = GetCurrentHandling(v->m_pVehicle);
    if (!p) return lines;
    std::string cur;
    for (int i = 0; i < HNDX_COUNT; i++) {
        char b[64];
        snprintf(b, sizeof(b), "%s=%.3f  ", g_HndExtFields[i].name, *(float*)(p + g_HndExtFields[i].off));
        cur += b;
        if ((i % 5) == 4) { lines.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

// ---- slotlar (1..5), fayla yazilir: SAMP/handling_slots.txt ----
#define HNDX_SLOTS 5
static std::vector<uint8_t> g_HndSlots[HNDX_SLOTS + 1];
static bool g_HndSlotsLoaded = false;

static void HndxSlotsWrite()
{
    FILE* f = fopen(HndxStorage("handling_slots.txt").c_str(), "w");
    if (!f) return;
    for (int i = 1; i <= HNDX_SLOTS; i++) {
        if (g_HndSlots[i].empty()) continue;
        fprintf(f, "%d ", i);
        for (uint8_t b : g_HndSlots[i]) fprintf(f, "%02X", b);
        fprintf(f, "\n");
    }
    fclose(f);
}

static void HndxSlotsRead()
{
    if (g_HndSlotsLoaded) return;
    g_HndSlotsLoaded = true;
    FILE* f = fopen(HndxStorage("handling_slots.txt").c_str(), "r");
    if (!f) return;
    static char line[1024];
    while (fgets(line, sizeof(line), f)) {
        int slot = 0;
        char hex[900];
        if (sscanf(line, "%d %899s", &slot, hex) != 2) continue;
        if (slot < 1 || slot > HNDX_SLOTS) continue;
        std::vector<uint8_t> data;
        if (HndxParseHex(hex, strlen(hex), data) && data.size() == HND_SIZE) g_HndSlots[slot] = data;
    }
    fclose(f);
}

bool HndExt_SaveSlot(CVehicle* v, int slot)
{
    if (!v || !v->m_pVehicle || slot < 1 || slot > HNDX_SLOTS) return false;
    uintptr_t p = GetCurrentHandling(v->m_pVehicle);
    if (!p) return false;
    HndxSlotsRead();
    g_HndSlots[slot].assign((uint8_t*)p, (uint8_t*)p + HND_SIZE);
    HndxSlotsWrite();
    return true;
}

bool HndExt_LoadSlot(CVehicle* v, int slot)
{
    if (!v || !v->m_pVehicle || slot < 1 || slot > HNDX_SLOTS) return false;
    HndxSlotsRead();
    if (g_HndSlots[slot].size() != HND_SIZE) return false;
    uintptr_t p = GetOrCreateOwnHandling(v->m_pVehicle);
    if (!p) return false;
    // ilk 4 bayt = masinin handling ID-si, toxunulmur
    memcpy((void*)(p + 4), g_HndSlots[slot].data() + 4, HND_SIZE - 4);
    return true;
}

// ---- handling kodu: "H1" + her sahe ucun 8 hex (float bitleri) ----
std::string HndExt_GetCode(CVehicle* v)
{
    if (!v || !v->m_pVehicle) return "";
    uintptr_t p = GetCurrentHandling(v->m_pVehicle);
    if (!p) return "";
    std::string s = "H1";
    for (int i = 0; i < HNDX_COUNT; i++) {
        uint32_t bits;
        memcpy(&bits, (void*)(p + g_HndExtFields[i].off), 4);
        char b[9];
        snprintf(b, sizeof(b), "%08X", bits);
        s += b;
    }
    FILE* f = fopen(HndxStorage("handling_code.txt").c_str(), "w");
    if (f) { fprintf(f, "%s\n", s.c_str()); fclose(f); }
    return s;
}

bool HndExt_SetCode(CVehicle* v, const std::string& code)
{
    if (!v || !v->m_pVehicle) return false;
    if (code.size() != (size_t)(2 + 8 * HNDX_COUNT) || code[0] != 'H' || code[1] != '1') return false;
    std::vector<uint8_t> data;
    if (!HndxParseHex(code.c_str() + 2, code.size() - 2, data)) return false;
    uintptr_t p = GetOrCreateOwnHandling(v->m_pVehicle);
    if (!p) return false;
    for (int i = 0; i < HNDX_COUNT; i++) {
        uint32_t bits = ((uint32_t)data[i * 4] << 24) | ((uint32_t)data[i * 4 + 1] << 16) |
                        ((uint32_t)data[i * 4 + 2] << 8) | (uint32_t)data[i * 4 + 3];
        memcpy((void*)(p + g_HndExtFields[i].off), &bits, 4);
    }
    HndxFixMass(p);
    return true;
}

// ---- cfg: SAMP/<ad>.cfg, her setir "sahe=deyer" (; ve # serhdir) ----
// qaytarir: tetbiq olunan sahe sayi, -1 = fayl yoxdur, -2 = yanlis ad
int HndExt_LoadCfg(CVehicle* v, const char* name)
{
    if (!v || !v->m_pVehicle) return -1;
    std::string n = (name && *name) ? name : "handling_user.cfg";
    if (n.find('/') != std::string::npos || n.find("..") != std::string::npos) return -2;
    if (n.size() < 4 || strcasecmp(n.c_str() + n.size() - 4, ".cfg") != 0) n += ".cfg";
    FILE* f = fopen(HndxStorage(n.c_str()).c_str(), "r");
    if (!f) return -1;
    int applied = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* c = strpbrk(line, ";#");
        if (c) *c = 0;
        char key[64];
        float val;
        if (sscanf(line, " %63[^= \t]%*[= \t]%f", key, &val) == 2) {
            if (HndExt_Set(v, key, val)) applied++;
        }
    }
    fclose(f);
    return applied;
}

#pragma clang diagnostic pop
// ===== /HNDEXT_PATCH8 =====
'''

# ====================================================================
# C++ : chat.cpp
# ====================================================================
CHAT_TOP = r'''
// ===== HNDEXT_PATCH8 =====
#include <vector>
std::string HndExt_FieldList();
bool HndExt_Set(CVehicle*, const char*, float);
bool HndExt_Get(CVehicle*, const char*, float*);
std::vector<std::string> HndExt_DumpLines(CVehicle*);
bool HndExt_SaveSlot(CVehicle*, int);
bool HndExt_LoadSlot(CVehicle*, int);
std::string HndExt_GetCode(CVehicle*);
bool HndExt_SetCode(CVehicle*, const std::string&);
int HndExt_LoadCfg(CVehicle*, const char*);

static CVehicle* HndExt_LocalVehicle()
{
    CPlayerPed* p = pGame ? pGame->FindPlayerPed() : nullptr;
    if (!p || !p->IsInVehicle() || !p->m_pPed) return nullptr;
    if (!pNetGame || !pNetGame->GetVehiclePool()) return nullptr;
    CVehiclePool* pool = pNetGame->GetVehiclePool();
    VEHICLEID id = pool->FindIDFromGtaPtr(p->m_pPed->pVehicle);
    if (id == INVALID_VEHICLE_ID) return nullptr;
    CVehicle* v = pool->GetAt(id);
    return (v && v->m_pVehicle) ? v : nullptr;
}
// ===== /HNDEXT_PATCH8 =====
'''

CHAT_CMD = r'''if (cmd == "/hlist" || cmd == "/hset" || cmd == "/hget" || cmd == "/hsave" ||
            cmd == "/hload" || cmd == "/hcode" || cmd == "/hcfg") {   // HNDEXT_PATCH8
                if (cmd == "/hlist") {
                        pUI->chat()->addDebugMessage("{B9C9BF}[Handling]: %s", HndExt_FieldList().c_str());
                        pUI->chat()->addDebugMessage("{B9C9BF}/hset sahe deyer | /hget [sahe] | /hsave 1-5 | /hload 1-5 | /hcode [kod] | /hcfg [ad]");
                        return true;
                }
                CVehicle* hv = HndExt_LocalVehicle();
                if (!hv) {
                        pUI->chat()->addDebugMessage("{FF0000}[Handling]: Avtomobilde deyilsiniz!");
                        return true;
                }
                std::string a1, a2;
                ss >> a1 >> a2;

                if (cmd == "/hset") {
                        if (a1.empty() || a2.empty()) {
                                pUI->chat()->addDebugMessage("{FF0000}[Handling]: /hset sahe deyer  (sahe siyahisi: /hlist)");
                        } else if (HndExt_Set(hv, a1.c_str(), (float)atof(a2.c_str()))) {
                                pUI->chat()->addDebugMessage("{00FF00}[Handling]: %s = %s", a1.c_str(), a2.c_str());
                        } else {
                                pUI->chat()->addDebugMessage("{FF0000}[Handling]: Namelum sahe (/hlist)");
                        }
                } else if (cmd == "/hget") {
                        if (a1.empty()) {
                                std::vector<std::string> ls = HndExt_DumpLines(hv);
                                for (size_t i = 0; i < ls.size(); i++)
                                        pUI->chat()->addDebugMessage("{B9C9BF}%s", ls[i].c_str());
                        } else {
                                float fv = 0.0f;
                                if (HndExt_Get(hv, a1.c_str(), &fv))
                                        pUI->chat()->addDebugMessage("{00FF00}[Handling]: %s = %.3f", a1.c_str(), fv);
                                else
                                        pUI->chat()->addDebugMessage("{FF0000}[Handling]: Namelum sahe (/hlist)");
                        }
                } else if (cmd == "/hsave") {
                        int sl = atoi(a1.c_str());
                        if (HndExt_SaveSlot(hv, sl))
                                pUI->chat()->addDebugMessage("{00FF00}[Handling]: Slot %d yadda saxlanildi", sl);
                        else
                                pUI->chat()->addDebugMessage("{FF0000}[Handling]: /hsave 1-5");
                } else if (cmd == "/hload") {
                        int sl = atoi(a1.c_str());
                        if (HndExt_LoadSlot(hv, sl))
                                pUI->chat()->addDebugMessage("{00FF00}[Handling]: Slot %d yuklendi", sl);
                        else
                                pUI->chat()->addDebugMessage("{FF0000}[Handling]: Slot bosdur ve ya yanlisdir (1-5)");
                } else if (cmd == "/hcode") {
                        if (a1.empty()) {
                                std::string code = HndExt_GetCode(hv);
                                pUI->chat()->addDebugMessage("{00FF00}[Handling]: Kod (SAMP/handling_code.txt-ye da yazildi):");
                                pUI->chat()->addDebugMessage("{B9C9BF}%s", code.c_str());
                        } else if (HndExt_SetCode(hv, a1)) {
                                pUI->chat()->addDebugMessage("{00FF00}[Handling]: Kod tetbiq olundu");
                        } else {
                                pUI->chat()->addDebugMessage("{FF0000}[Handling]: Kod yanlisdir");
                        }
                } else if (cmd == "/hcfg") {
                        int r = HndExt_LoadCfg(hv, a1.c_str());
                        if (r >= 0)
                                pUI->chat()->addDebugMessage("{00FF00}[Handling]: cfg yuklendi, %d sahe tetbiq olundu", r);
                        else if (r == -2)
                                pUI->chat()->addDebugMessage("{FF0000}[Handling]: Yanlis fayl adi");
                        else
                                pUI->chat()->addDebugMessage("{FF0000}[Handling]: SAMP/%s tapilmadi", a1.empty() ? "handling_user.cfg" : a1.c_str());
                }
                return true;
        }

        '''

def patch_vehicle():
    s = rd(VEH)
    if "HNDEXT_PATCH8" in s: return False
    if "GetOrCreateOwnHandling" not in s: return False
    wr(VEH, s.rstrip("\n") + "\n" + VEH_BLOCK)
    return True

def patch_chat():
    s = rd(CHAT)
    if "HNDEXT_PATCH8" in s: return False
    m1 = re.search(r'^bool g_bShowHandlingDlg\s*=\s*false;[^\n]*\n', s, re.M)
    m2 = re.search(r'^([ \t]*)if \(cmd == "/mm"\) \{', s, re.M)
    if not m1 or not m2: return False
    # once komandalar (sonra offset deyismesin deye), sonra funksiyalar
    s = s[:m2.start()] + m2.group(1) + CHAT_CMD + s[m2.start() + len(m2.group(1)):]
    s = s[:m1.end()] + CHAT_TOP + s[m1.end():]
    wr(CHAT, s)
    return True

# ====================================================================
# Java : SplashActivity - yeni loading animasiyalari
# ====================================================================
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

def patch_splash():
    s = rd(SPL)
    if "SPLASH_ANIM_PATCH8" in s: return False
    m = re.search(r'setContentView\(R\.layout\.activity_splash\);[^\n]*\n', s)
    if not m: return False
    wr(SPL, s[:m.end()] + SPLASH_ADD + s[m.end():])
    return True

# ====================================================================
# Java : LobbyActivity - arxa fon musiqisi (res/raw/bg_music1..3)
# ====================================================================
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

def patch_lobby():
    s = rd(LOB)
    if "LOBBY_MUSIC_PATCH8" in s: return False
    if re.search(r'void onPause\(\)|void onDestroy\(\)', s): return False   # toqqusma olmasin
    m = re.search(r'([ \t]*@Override\s*\n[ \t]*protected void onResume\(\) \{\s*\n[ \t]*super\.onResume\(\);)', s)
    if not m: return False
    new = MUSIC + m.group(1) + "\n        startBgMusic();"
    wr(LOB, s[:m.start()] + new + s[m.end():])
    os.makedirs(os.path.join(ROOT, RAW), exist_ok=True)
    with open(os.path.join(ROOT, RAW, "README_music.txt"), "w", encoding="utf-8") as f:
        f.write("Bura 3 mahni qoy: bg_music1.mp3, bg_music2.mp3, bg_music3.mp3\n(kicik herfler, bosluqsuz). Olmayan slot kecilir.\n")
    return True

run("Merhele 2: vehicle.cpp handling funksiyalari", patch_vehicle)
run("Merhele 2: chat.cpp /hset /hget /hsave /hload /hcode /hcfg /hlist", patch_chat)
run("Merhele 7: SplashActivity loading animasiyasi", patch_splash)
run("Merhele 7: LobbyActivity arxa fon musiqisi", patch_lobby)
print("\nBitdi. Geri qaytarmaq ucun her faylin yaninda *.bak8 var.")
