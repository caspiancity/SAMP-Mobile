
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_patch56.py -- repo-nun KOK qovluguna qoy:
    python3 apply_patch56.py
    python3 apply_patch56.py --dry

apply_patch5.py + apply_patch6.py BIRLESDIRILMIS VERSIYA

PATCH 5:
  1) /mm masinsiz da acilir.
  2) Masin bolmeleri masin olmadiginda deaktiv edilir.
  3) "Masinda deyilsiniz" overlay gosterilir.
  4) Chat yuxari-sol kuncə cekilir, 18% daraldilir.
  5) Sol ve yuxari bosluqlar piksel olaraq eynidir.

PATCH 6:
  1) Serbest RGB far rengi.
  2) Yere dusen far isigi ve headlightline koronalarinin renglenmesi.
  3) /far R G B, /far #RRGGBB, /far off.
  4) /mm Far sehifesinde RGB slider, preview, palette, Standart.
  5) Minimap gizledilir, /radar ile acilib-baglanir.
  6) /dump son setirleri chata da yazir.

XUSUSIYYETLER:
  - gui.cpp ve chat.cpp yalniz bir defe emal olunur.
  - Her iki patch-in deyisiklikleri qorunur.
  - Butun deyisiklikler yoxlanildiqdan sonra yazilir.
  - Backup: .patchbak56/
  - --dry rejimi.
"""

import os
import re
import sys
import shutil


CPP = "app/src/main/cpp/samp"
DRY = "--dry" in sys.argv


class PatchError(Exception):
    pass


def rd(p):
    with open(p, encoding="utf-8", newline="") as f:
        return f.read()


def wr(p, t):
    with open(p, "w", encoding="utf-8", newline="") as f:
        f.write(t)


def flex(a):
    return re.compile(r"\s+".join(re.escape(x) for x in a.split()))


def find1(t, anchor, what):
    ms = list(flex(anchor).finditer(t))
    if len(ms) != 1:
        raise PatchError(
            "%s: anchor %d defe tapildi (1 olmali): %s"
            % (what, len(ms), anchor[:70])
        )
    return ms[0]


def sub1(t, anchor, repl, what):
    m = find1(t, anchor, what)
    return t[:m.start()] + repl + t[m.end():]


def before(t, anchor, block, what):
    m = find1(t, anchor, what)
    return t[:m.start()] + block + t[m.start():]


# ============================================================
# PATCH 5
# ============================================================


# ------------------------------------------------------------
# PATCH 5: gui.cpp
# ------------------------------------------------------------

OVERLAY = r'''if (noVeh && (s_page <= 4 || s_page == 6))
                {
                        // masina yoxdur: bu bolme sondurulub
                        const Rc ov{ cx0 - S(8.0f), P.y + S(112.0f), cw + S(16.0f), P.h - S(128.0f) };
                        Fill(ov, Col(17, 19, 26, 205), S(12.0f));
                        TxtC(S(18.0f), Rc{ ov.x, ov.y + ov.h * 0.5f - S(30.0f), ov.w, S(26.0f) }, Col(238, 241, 248), "Masinda deyilsiniz", true);
                        TxtC(S(14.0f), Rc{ ov.x, ov.y + ov.h * 0.5f + S(2.0f), ov.w, S(22.0f) }, Col(124, 132, 152), "Bu bolme yalniz masinda isleyir", false);
                }
                canInput = canInputAll;

                s_A = saveA;'''


def patch_gui5(t):
    if "MM_NO_VEHICLE_PATCH" in t:
        return t, False

    t = sub1(
        t,
        "CVehicle* pVehW = nullptr;",
        "CVehicle*    pVehW  = nullptr;\n"
        "        bool         noVeh  = false;   // MM_NO_VEHICLE_PATCH",
        "noVeh decl"
    )

    t = sub1(
        t,
        "const bool canInput = want && s_t > 0.75f;",
        "bool canInput = want && s_t > 0.75f;",
        "canInput mutable"
    )

    t = sub1(
        t,
        "if (!pLocalPlayer || !pLocalPlayer->IsInVehicle()) { "
        "g_bShowHandlingDlg = false; "
        "s_t = 0.0f; s_wasOpen = false; "
        "s_hndRectValid = false; s_act = 0; return; }",

        "noVeh = !pLocalPlayer || !pLocalPlayer->IsInVehicle();\n"
        "                if (noVeh) {\n"
        "                        gtaVeh = nullptr; s_lastVeh = nullptr;\n"
        "                        s_eng = s_lig = s_lock = false; s_sig = 0;\n"
        "                        s_wasOpen = true;\n"
        "                } else {",

        "gui no-vehicle block"
    )

    t = sub1(
        t,
        "s_sig = VehControls::GetSignal(gtaVeh);",
        "s_sig = VehControls::GetSignal(gtaVeh);\n                }",
        "gui close else"
    )

    t = sub1(
        t,
        "// --- mezmun ---",
        "const bool canInputAll = canInput;\n"
        "        if (noVeh && (s_page <= 4 || s_page == 6)) "
        "canInput = false;   // masina bolmelerinde toxunus yoxdur\n\n"
        "        // --- mezmun ---",
        "gui disable input"
    )

    t = sub1(
        t,
        "s_A = saveA;",
        OVERLAY,
        "gui overlay"
    )

    return t, True


# ------------------------------------------------------------
# PATCH 5: chat.cpp
# ------------------------------------------------------------

def patch_chat5(t):
    pat = re.compile(
        r'if \(cmd == "/mm"\) \{.*?g_bShowHandlingDlg = !g_bShowHandlingDlg;',
        re.S
    )

    ms = list(pat.finditer(t))

    if len(ms) != 1:
        if "MM_NO_VEHICLE_PATCH" in t:
            return t, False

        raise PatchError(
            'chat.cpp: "/mm" bloku %d defe tapildi' % len(ms)
        )

    blk = ms[0].group(0)

    if "IsInVehicle" not in blk:
        return t, False

    new = (
        'if (cmd == "/mm") {   '
        '// MM_NO_VEHICLE_PATCH: artiq masinsiz da acilir\n'
        '                g_bShowHandlingDlg = !g_bShowHandlingDlg;'
    )

    return (
        t[:ms[0].start()] + new + t[ms[0].end():],
        True
    )


# ------------------------------------------------------------
# PATCH 5: uisettings.cpp
# ------------------------------------------------------------

def patch_ui5(t):
    if "CHAT_CORNER_PATCH" in t:
        return t, False

    t = sub1(
        t,
        "ImVec2 UISettings::m_chatSize = ImVec2(400.0f, 0.0f);",
        "ImVec2 UISettings::m_chatSize = ImVec2(330.0f, 0.0f);",
        "chat size"
    )

    t = sub1(
        t,
        "ImVec2 UISettings::m_chatItemSize = ImVec2(400.0f, 13.0f);",
        "ImVec2 UISettings::m_chatItemSize = ImVec2(330.0f, 13.0f);",
        "chat item size"
    )

    t = sub1(
        t,
        "m_chatPos = m_chatPos * m_scaleFactor;",
        "// CHAT_CORNER_PATCH: yuxari-sol kunc, "
        "sol ve yuxari bosluq pikselde EYNIDIR\n"
        "\tm_chatPos = ImVec2("
        "16.0f * m_scaleFactor.y, "
        "16.0f * m_scaleFactor.y);",
        "chat pos"
    )

    return t, True


# ============================================================
# PATCH 6
# ============================================================


# ------------------------------------------------------------
# PATCH 6: Coronas.h
# ------------------------------------------------------------

H_DECL = r'''static inline CoronaHeadlightOverride s_HeadlightOverride{};

    // HEADLIGHT_RGB_PATCH: masin ucun serbest far rengi
    // (CCoronas::Set... UI ve /far istifade edir)
    static void SetHeadlightRGB(const void* veh, uint8 r, uint8 g, uint8 b);
    static void ClearHeadlightRGB(const void* veh);
    static bool GetHeadlightRGB(const void* veh, uint8& r, uint8& g, uint8& b);'''


def patch_coronas_h(t):
    if "HEADLIGHT_RGB_PATCH" in t:
        return t, False

    return sub1(
        t,
        "static inline CoronaHeadlightOverride s_HeadlightOverride{};",
        H_DECL,
        "Coronas.h"
    ), True


# ------------------------------------------------------------
# PATCH 6: Coronas.cpp
# ------------------------------------------------------------

C_IMPL = r'''// HEADLIGHT_RGB_PATCH ------------------------------------------------------
namespace {
    struct HlEntry {
        uint32_t ct;
        uint8 r, g, b;
    };

    std::unordered_map<const void*, HlEntry> g_hlColors;
}

void CCoronas::SetHeadlightRGB(
    const void* veh,
    uint8 r,
    uint8 g,
    uint8 b
) {
    if (!veh) return;

    g_hlColors[veh] = HlEntry{
        (uint32_t)static_cast<const CVehicleGTA*>(veh)->m_nCreationTime,
        r, g, b
    };
}

void CCoronas::ClearHeadlightRGB(const void* veh) {
    g_hlColors.erase(veh);
}

bool CCoronas::GetHeadlightRGB(
    const void* veh,
    uint8& r,
    uint8& g,
    uint8& b
) {
    if (!veh || g_hlColors.empty())
        return false;

    auto it = g_hlColors.find(veh);

    if (it == g_hlColors.end())
        return false;

    if (it->second.ct !=
        (uint32_t)static_cast<const CVehicleGTA*>(veh)->m_nCreationTime) {

        // masina yeniden yaranib
        g_hlColors.erase(it);
        return false;
    }

    r = it->second.r;
    g = it->second.g;
    b = it->second.b;

    return true;
}


// Yerdeki far isigi: CShadows::StoreCarLightShadow.
// Qirmizi (arxa) kolgelere toxunulmur.

using StoreCarLightShadowFn = void (*)(
    void*, int, RwTexture*, CVector*,
    float, float, float, float,
    uint8, uint8, uint8, float
);

static StoreCarLightShadowFn CShadows__StoreCarLightShadow = nullptr;


static void StoreCarLightShadow_hook(
    void* veh,
    int id,
    RwTexture* tex,
    CVector* posn,
    float a,
    float b,
    float c,
    float d,
    uint8 r,
    uint8 g,
    uint8 bl,
    float maxViewAngle
) {
    const bool redDominant =
        (r > g * 2 + 10) &&
        (r > bl * 2 + 10);

    if (veh && !redDominant) {
        uint8 cr = 0, cg = 0, cb = 0;

        bool have =
            CCoronas::GetHeadlightRGB(veh, cr, cg, cb);

        if (!have) {
            const auto& ho = CCoronas::s_HeadlightOverride;

            if (ho.active && !ho.inTail && ho.vehicle == veh) {
                cr = ho.r;
                cg = ho.g;
                cb = ho.b;
                have = true;
            }
        }

        if (have) {
            const int mx = std::max(
                (int)r,
                std::max((int)g, (int)bl)
            );

            const float k = (float)mx / 255.0f;

            r  = (uint8)((float)cr * k);
            g  = (uint8)((float)cg * k);
            bl = (uint8)((float)cb * k);
        }
    }

    CShadows__StoreCarLightShadow(
        veh, id, tex, posn,
        a, b, c, d,
        r, g, bl,
        maxViewAngle
    );
}


void CCoronas::InjectHooks() {'''


C_SHADOW_INSTALL = r'''// Yer isigi (kolge) rengi
    CHook::InlineHook(
        "_ZN8CShadows19StoreCarLightShadowEP8CVehicleiP9RwTextureP7CVectorffffhhhf",
        &StoreCarLightShadow_hook,
        &CShadows__StoreCarLightShadow
    );

   // CHook::InlineHook("_ZN8CCoronas4InitEv", &CCoronas::Init, &CCoronas__Init);'''


def patch_coronas_cpp(t):
    if "HEADLIGHT_RGB_PATCH" in t:
        return t, False

    t = sub1(
        t,
        '#include "TxdStore.h"',
        '#include "TxdStore.h"\n'
        '#include "Entity/CVehicleGTA.h"\n'
        '#include <unordered_map>\n'
        '#include <algorithm>',
        "Coronas.cpp include"
    )

    t = sub1(
        t,
        "void CCoronas::InjectHooks() {",
        C_IMPL,
        "Coronas.cpp impl"
    )

    t = sub1(
        t,
        "&& coronaType == eCoronaType::CORONATYPE_HEADLIGHT",
        "&& (coronaType == eCoronaType::CORONATYPE_HEADLIGHT || "
        "coronaType == eCoronaType::CORONATYPE_HEADLIGHTLINE)",
        "Coronas.cpp types"
    )

    t = sub1(
        t,
        '// CHook::InlineHook("_ZN8CCoronas4InitEv", '
        '&CCoronas::Init, &CCoronas__Init);',
        C_SHADOW_INSTALL,
        "Coronas.cpp install"
    )

    return t, True


# ------------------------------------------------------------
# PATCH 6: CVehicleGTA.cpp
# ------------------------------------------------------------

def patch_vehgta(t):
    if "CCoronas::GetHeadlightRGB" in t:
        return t, False

    return sub1(
        t,
        "ho.active = VehTuner::GetHeadlightRGB(thiz, ho.r, ho.g, ho.b);",
        "ho.active = CCoronas::GetHeadlightRGB(thiz, ho.r, ho.g, ho.b) ||\n"
        "                VehTuner::GetHeadlightRGB(thiz, ho.r, ho.g, ho.b);",
        "CVehicleGTA far"
    ), True


# ------------------------------------------------------------
# PATCH 6: patches.cpp (MINIMAP)
# ------------------------------------------------------------

P_HOOK = r'''// MINIMAP_OFF_PATCH: minimap gizlidir. /radar ile acib-bagla.
bool g_bHideMinimap = true;

static void (*CHud__DrawRadar_orig)() = nullptr;

static void CHud__DrawRadar_hook()
{
    if (g_bHideMinimap)
        return;

    CHud__DrawRadar_orig();
}

void ApplyGlobalPatches()'''


def patch_patches(t):
    if "MINIMAP_OFF_PATCH" in t:
        return t, False

    t = sub1(
        t,
        "void ApplyGlobalPatches()",
        P_HOOK,
        "patches.cpp hook"
    )

    t = sub1(
        t,
        'CHook::RET("_ZN6CRadar10DrawLegendEiii");',
        'CHook::RET("_ZN6CRadar10DrawLegendEiii");\n'
        '    CHook::InlineHook("_ZN4CHud9DrawRadarEv", '
        '&CHud__DrawRadar_hook, &CHud__DrawRadar_orig); '
        '// MINIMAP_OFF_PATCH',
        "patches.cpp install"
    )

    return t, True


# ------------------------------------------------------------
# PATCH 6: chat.cpp
# ------------------------------------------------------------

CHAT_RADAR = r'''if (cmd == "/radar") {   // FAR_RGB_PATCH
                g_bHideMinimap = !g_bHideMinimap;
                pUI->chat()->addDebugMessage(
                    "{00FF00}[Client]: Minimap: %s",
                    g_bHideMinimap ? "gizli" : "aciq"
                );
                return true;
        }

        if (cmd == "/gizle") {'''


CHAT_FAR = r'''if (cmd == "/far")
        {
                CPlayerPed* pLocalPlayer =
                    pGame ? pGame->FindPlayerPed() : nullptr;

                if (!pLocalPlayer || !pLocalPlayer->IsInVehicle()) {
                        pUI->chat()->addDebugMessage(
                            "{FF0000}[Client]: Xeta: Avtomobilde deyilsiniz!"
                        );
                        return true;
                }

                CVehicle* pVeh = nullptr;

                if (pNetGame &&
                    pNetGame->GetVehiclePool() &&
                    pLocalPlayer->m_pPed) {

                        CVehiclePool* pPool =
                            pNetGame->GetVehiclePool();

                        VEHICLEID vehID =
                            pPool->FindIDFromGtaPtr(
                                pLocalPlayer->m_pPed->pVehicle
                            );

                        if (vehID != INVALID_VEHICLE_ID)
                            pVeh = pPool->GetAt(vehID);
                }

                if (!pVeh || !pVeh->m_pVehicle) {
                        pUI->chat()->addDebugMessage(
                            "{FF0000}[Client]: Avtomobil tapilmadi!"
                        );
                        return true;
                }

                std::string a1;
                ss >> a1;

                std::transform(
                    a1.begin(), a1.end(),
                    a1.begin(), ::tolower
                );

                if (a1 == "off" || a1 == "0" || a1 == "standart") {
                        CCoronas::ClearHeadlightRGB(pVeh->m_pVehicle);

                        pUI->chat()->addDebugMessage(
                            "{00FF00}[Client]: Far rengi standarta qaytarildi."
                        );

                        return true;
                }

                int fr = -1, fg = -1, fb = -1;

                if (a1.size() == 7 && a1[0] == '#') {
                        const unsigned long hv =
                            strtoul(a1.c_str() + 1, nullptr, 16);

                        fr = (int)((hv >> 16) & 255);
                        fg = (int)((hv >> 8) & 255);
                        fb = (int)(hv & 255);

                } else if (!a1.empty()) {
                        std::string a2, a3;

                        ss >> a2 >> a3;

                        if (!a2.empty() && !a3.empty()) {
                                fr = atoi(a1.c_str());
                                fg = atoi(a2.c_str());
                                fb = atoi(a3.c_str());
                        }
                }

                if (fr < 0 || fg < 0 || fb < 0 ||
                    fr > 255 || fg > 255 || fb > 255) {

                        pUI->chat()->addDebugMessage(
                            "{FF0000}[Client]: Istifade: "
                            "/far R G B (0-255) ve ya "
                            "/far #RRGGBB ve ya /far off"
                        );

                        return true;
                }

                CCoronas::SetHeadlightRGB(
                    pVeh->m_pVehicle,
                    (uint8_t)fr,
                    (uint8_t)fg,
                    (uint8_t)fb
                );

                pUI->chat()->addDebugMessage(
                    "{00FF00}[Client]: Far rengi: #%02X%02X%02X",
                    fr, fg, fb
                );

                return true;
        }

        if (cmd == "/far" || cmd == "/suret" || cmd == "/manevr" || cmd == "/tormoz")'''


CHAT_DUMP = r'''{
                        pUI->chat()->addDebugMessage(
                            "{00FF00}[Client]: Yazildi: %s",
                            path.c_str()
                        );

                        // son setirleri chata da yaz
                        FILE* df = fopen(path.c_str(), "rb");

                        if (df) {
                                fseek(df, 0, SEEK_END);

                                const long dsz = ftell(df);
                                const long doff =
                                    dsz > 1400 ? dsz - 1400 : 0;

                                fseek(df, doff, SEEK_SET);

                                char tb[1500];

                                const size_t dn =
                                    fread(tb, 1, sizeof(tb) - 1, df);

                                tb[dn] = 0;

                                fclose(df);

                                char* dp = tb;

                                if (doff > 0) {
                                        char* nl = strchr(tb, '\n');
                                        if (nl) dp = nl + 1;
                                }

                                while (*dp) {
                                        char* de = strchr(dp, '\n');

                                        if (de)
                                            *de = 0;

                                        if (*dp) {
                                                pUI->chat()->addDebugMessage(
                                                    "{B9C9BF}%s", dp
                                                );
                                        }

                                        if (!de)
                                            break;

                                        dp = de + 1;
                                }
                        }
                }'''


def patch_chat6(t):
    if "FAR_RGB_PATCH" in t:
        return t, False

    t = sub1(
        t,
        "#include <sstream>",
        "#include <sstream>\n"
        "#include <cstdio>\n"
        "#include <cstring>\n"
        "#include <cstdlib>",
        "chat include1"
    )

    t = sub1(
        t,
        '#include "../../game/AzPlate.h"',
        '#include "../../game/AzPlate.h"\n'
        '#include "../../game/Coronas.h"',
        "chat include2"
    )

    t = sub1(
        t,
        "bool g_bHideAllUI = false;",
        "bool g_bHideAllUI = false;\n"
        "extern bool g_bHideMinimap; "
        "// FAR_RGB_PATCH (patches.cpp)",
        "chat extern"
    )

    t = sub1(
        t,
        'if (cmd == "/gizle") {',
        CHAT_RADAR,
        "chat radar"
    )

    t = sub1(
        t,
        'if (cmd == "/far" || cmd == "/suret" || '
        'cmd == "/manevr" || cmd == "/tormoz")',
        CHAT_FAR,
        "chat far"
    )

    t = sub1(
        t,
        'pUI->chat()->addDebugMessage('
        '"{00FF00}[Client]: Yazildi: %s", path.c_str());',
        CHAT_DUMP,
        "chat dump"
    )

    return t, True


# ------------------------------------------------------------
# PATCH 6: gui.cpp (FAR PAGE)
# ------------------------------------------------------------

FAR_PAGE = r'''else if (s_page == 4)     // FAR (serbest RGB) FAR_RGB_PATCH
                {
                        static CVehicleGTA* s_hlVeh = nullptr;
                        static float s_hl[3] = {
                            255.0f, 255.0f, 255.0f
                        };

                        if (gtaVeh != s_hlVeh) {
                                s_hlVeh = gtaVeh;

                                uint8_t r8 = 255, g8 = 255, b8 = 255;

                                if (gtaVeh &&
                                    CCoronas::GetHeadlightRGB(
                                        gtaVeh, r8, g8, b8
                                    )) {

                                        s_hl[0] = r8;
                                        s_hl[1] = g8;
                                        s_hl[2] = b8;

                                } else {
                                        s_hl[0] =
                                        s_hl[1] =
                                        s_hl[2] = 255.0f;
                                }
                        }

                        static const char* kRGB[3] = {
                            "Qirmizi (R)",
                            "Yasil (G)",
                            "Mavi (B)"
                        };

                        bool hlChanged = false;

                        for (int i = 0; i < 3; ++i) {
                                const Rc row{
                                    cx0,
                                    P.y + S(124.0f + 72.0f * (float)i),
                                    cw,
                                    S(66.0f)
                                };

                                if (SliderRow(
                                    i, row, kRGB[i],
                                    &s_hl[i],
                                    0.0f, 255.0f,
                                    dt, canInput,
                                    "", 5.0f
                                )) {
                                        hlChanged = true;
                                }
                        }

                        // onizleme + HEX + standart
                        const Rc pvr{
                            cx0,
                            P.y + S(346.0f),
                            S(150.0f),
                            S(44.0f)
                        };

                        Fill(
                            pvr,
                            Col(
                                (int)s_hl[0],
                                (int)s_hl[1],
                                (int)s_hl[2],
                                255
                            ),
                            S(10.0f)
                        );

                        Stroke(
                            pvr,
                            Col(255, 255, 255, 50),
                            S(10.0f),
                            S(1.2f)
                        );

                        char hx[24];

                        snprintf(
                            hx, sizeof(hx),
                            "#%02X%02X%02X",
                            (int)s_hl[0],
                            (int)s_hl[1],
                            (int)s_hl[2]
                        );

                        Txt(
                            S(18.0f),
                            cx0 + S(166.0f),
                            P.y + S(356.0f),
                            Col(214, 220, 232),
                            hx
                        );

                        const Rc bStd{
                            cx0 + S(300.0f),
                            P.y + S(346.0f),
                            S(150.0f),
                            S(44.0f)
                        };

                        if (Tap(90, bStd, canInput) && gtaVeh) {
                                CCoronas::ClearHeadlightRGB(gtaVeh);

                                s_tune.headlight = 0;
                                changed = true;

                                s_hl[0] =
                                s_hl[1] =
                                s_hl[2] = 255.0f;
                        }

                        {
                                const float p = Prs(90, dt);
                                const Rc d = Shrink(
                                    bStd, S(1.5f) * p
                                );

                                Fill(
                                    d,
                                    Col(255, 255, 255,
                                        14 + (int)(30.0f * p)),
                                    S(9.0f)
                                );

                                Stroke(
                                    d,
                                    Col(255, 255, 255, 40),
                                    S(9.0f),
                                    S(1.2f)
                                );

                                TxtC(
                                    S(16.0f),
                                    d,
                                    Col(240, 244, 252),
                                    "Standart",
                                    true
                                );
                        }

                        // hazir rengler
                        for (int i = 0;
                             i < VEHTUNER_COLOR_COUNT;
                             ++i) {

                                uint8_t r, g, b;

                                VehTuner::GetPaletteColor(
                                    i, r, g, b
                                );

                                const int col = i % 8;
                                const int rowi = i / 8;

                                const float cxs =
                                    cx0 + S(24.0f) +
                                    (float)col * S(58.0f);

                                const float cys =
                                    P.y + S(436.0f) +
                                    (float)rowi * S(54.0f);

                                const Rc sq{
                                    cxs - S(24.0f),
                                    cys - S(24.0f),
                                    S(48.0f),
                                    S(48.0f)
                                };

                                const int sid =
                                    ID_SWATCH + std::min(i, 23);

                                if (Tap(sid, sq, canInput) && gtaVeh) {
                                        s_hl[0] = r;
                                        s_hl[1] = g;
                                        s_hl[2] = b;
                                        hlChanged = true;
                                }

                                const float p = Prs(sid, dt);

                                Dot(
                                    cxs, cys,
                                    S(20.0f) - S(2.0f) * p,
                                    Col(r, g, b, 255)
                                );
                        }

                        if (hlChanged && gtaVeh) {
                                CCoronas::SetHeadlightRGB(
                                    gtaVeh,
                                    (uint8_t)s_hl[0],
                                    (uint8_t)s_hl[1],
                                    (uint8_t)s_hl[2]
                                );
                        }
                }
                else if (s_page == 5)    // KOMANDALAR'''


def patch_gui6(t):
    if "FAR_RGB_PATCH" in t:
        return t, False

    t = sub1(
        t,
        '#include "../game/AzPlate.h"',
        '#include "../game/AzPlate.h"\n'
        '#include "../game/Coronas.h"',
        "gui include"
    )

    t = sub1(
        t,
        "static bool SliderRow(int idx, const Rc& row, "
        "const char* label, float* v, float mn, float mx, "
        "float dt, bool canInput)",

        'static bool SliderRow(int idx, const Rc& row, '
        'const char* label, float* v, float mn, float mx, '
        'float dt, bool canInput, const char* unit = "%", '
        'float step = 5.0f)',

        "SliderRow sig"
    )

    t = sub1(
        t,
        "nv = Clamp(*v - 5.0f, mn, mx);",
        "nv = Clamp(*v - step, mn, mx);",
        "SliderRow minus"
    )

    t = sub1(
        t,
        "nv = Clamp(*v + 5.0f, mn, mx);",
        "nv = Clamp(*v + step, mn, mx);",
        "SliderRow plus"
    )

    t = sub1(
        t,
        'snprintf(buf, sizeof(buf), "%.0f%%", *v);',
        'snprintf(buf, sizeof(buf), "%.0f%s", *v, unit);',
        "SliderRow fmt"
    )

    t = sub1(
        t,
        "if (mn < 100.0f && mx > 100.0f) {",
        "if (mn < 100.0f && mx > 100.0f && unit[0] == '%') {",
        "SliderRow mark"
    )

    ms = find1(
        t,
        "else if (s_page == 4) // FAR",
        "far block start"
    )

    me = find1(
        t,
        "else if (s_page == 5) // KOMANDALAR",
        "far block end"
    )

    if me.start() <= ms.start():
        raise PatchError("far block sirasi sehvdir")

    t = t[:ms.start()] + FAR_PAGE + t[me.end():]

    return t, True


# ============================================================
# BIRLESdirilmis gui.cpp
# ============================================================

def patch_gui_combined(t):
    """
    Evvel PATCH 5, sonra PATCH 6.
    Her iki patch eyni gui.cpp daxilinde tetbiq edilir.
    """

    t, ch5 = patch_gui5(t)
    t, ch6 = patch_gui6(t)

    return t, ch5 or ch6


# ============================================================
# BIRLESdirilmis chat.cpp
# ============================================================

def patch_chat_combined(t):
    """
    Evvel PATCH 5 /mm deyisikliyi,
    sonra PATCH 6 /far, /radar ve /dump.
    """

    t, ch5 = patch_chat5(t)
    t, ch6 = patch_chat6(t)

    return t, ch5 or ch6


# ============================================================
# MAIN
# ============================================================

def main():

    if not os.path.isdir(CPP):
        print("XETA: repo-nun kok qovlugunda deyilsen")
        sys.exit(1)

    jobs = [

        # PATCH 5 + PATCH 6: gui.cpp
        (
            CPP + "/gui/gui.cpp",
            patch_gui_combined,
            True
        ),

        # PATCH 5 + PATCH 6: chat.cpp
        (
            CPP + "/gui/samp_widgets/chat.cpp",
            patch_chat_combined,
            True
        ),

        # PATCH 5: uisettings.cpp
        (
            CPP + "/gui/uisettings.cpp",
            patch_ui5,
            True
        ),

        # PATCH 6: far sistemi
        (
            CPP + "/game/Coronas.h",
            patch_coronas_h,
            True
        ),

        (
            CPP + "/game/Coronas.cpp",
            patch_coronas_cpp,
            True
        ),

        (
            CPP + "/game/Entity/CVehicleGTA.cpp",
            patch_vehgta,
            True
        ),

        # PATCH 6: minimap
        (
            CPP + "/game/patches.cpp",
            patch_patches,
            True
        ),
    ]

    res = []

    # --------------------------------------------------------
    # 1. BUTUN FAYLLARI OXU VE YOXLA
    # --------------------------------------------------------

    try:

        for p, fn, must in jobs:

            if not os.path.isfile(p):
                raise PatchError("fayl yoxdur: " + p)

            old = rd(p)

            new, ch = fn(old)

            res.append(
                (p, old, new, ch)
            )

    except PatchError as e:

        print(
            "XETA: %s\n"
            "Hec bir fayl deyisdirilmedi. "
            "Bu xetani mene gonder." % e
        )

        sys.exit(2)

    # --------------------------------------------------------
    # 2. NETICELERI GOSTER
    # --------------------------------------------------------

    print("\n========== PATCH 5 + PATCH 6 ==========\n")

    for p, old, new, ch in res:

        print(
            ("[DEYISDI] " if ch else "[KECILDI] ") + p
        )

    # --------------------------------------------------------
    # 3. DRY MODE
    # --------------------------------------------------------

    if DRY:

        print("\n--dry: hec bir fayla yazilmadi.")
        return

    # --------------------------------------------------------
    # 4. BACKUP
    # --------------------------------------------------------

    backup_dir = ".patchbak56"

    os.makedirs(backup_dir, exist_ok=True)

    for p, old, new, ch in res:

        if ch:

            backup_path = os.path.join(
                backup_dir,
                p.replace("/", "__")
            )

            shutil.copyfile(p, backup_path)

    # --------------------------------------------------------
    # 5. DEYISIKLIKLERI YAZ
    # --------------------------------------------------------

    try:

        for p, old, new, ch in res:

            if ch:
                wr(p, new)

    except Exception as e:

        print("\nXETA: fayllara yazilma zamani problem yarandi:", e)
        print("Backup qovlugu:", backup_dir)
        sys.exit(3)

    # --------------------------------------------------------
    # 6. .gitignore
    # --------------------------------------------------------

    gi = ".gitignore"

    have = rd(gi) if os.path.isfile(gi) else ""

    if ".patchbak56/" not in have.split():

        wr(
            gi,
            have +
            ("" if have.endswith("\n") or not have else "\n") +
            ".patchbak56/\n"
        )

    # --------------------------------------------------------
    # 7. SON
    # --------------------------------------------------------

    print("\n======================================")
    print("PATCH 5 + PATCH 6 HAZIRDIR!")
    print("======================================")

    print("\nBackup: .patchbak56/")

    print("\nTest:")

    print("  /mm")
    print("  /far 255 0 80")
    print("  /far #FF0050")
    print("  /far off")
    print("  /radar")
    print("  /dump")

    print("\nBuild et, sonra:")

    print("git add -A")
    print('git commit -m "patch5 patch6 combined"')
    print("git push")


if __name__ == "__main__":
    main()
