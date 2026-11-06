#!/usr/bin/env python3
# Repo kokunde isledin:  python3 apply_patch.py
# Edir: gui/gui.cpp  (MTA stilli speedometr + UI gizletme)
#       game/Entity/CVehicleGTA.cpp  (sinyal isiqlari)
# Her fayl evvelce .bak olaraq saxlanir. Anchor tapilmasa HEC NE deyismir.
import re, sys, shutil

ROOT = "app/src/main/cpp/samp"
GUI = ROOT + "/gui/gui.cpp"
VEH = ROOT + "/game/Entity/CVehicleGTA.cpp"


def load(p):
    t = open(p, "rb").read().decode("utf-8")
    crlf = "\r\n" in t
    return t.replace("\r\n", "\n"), crlf


def save(p, t, crlf):
    shutil.copyfile(p, p + ".bak")
    if crlf:
        t = t.replace("\n", "\r\n")
    open(p, "wb").write(t.encode("utf-8"))


def die(msg):
    print("XETA:", msg)
    print("Hec bir fayl deyismedi (ve ya yalniz evvelki fayl deyisdi), .bak-lara bax.")
    sys.exit(1)


# ----------------------------------------------------------------------------
NEW_HUD = r'''#include <cmath>
#include <cstdio>

// ============================================================================
//  MTA stilli dairevi speedometr. Sinyal UI legv edilib (hele ki).
//  Uzun basma = reng deyisimi (kHudPalette). Benzin dairesi YOXDUR:
//  client-de yanacaq melumati yoxdur (serverin textdrawidir).
// ============================================================================
void RenderVehicleHud()
{
        if (g_bHideAllUI || g_bShowHandlingDlg) return;

        CPlayerPed* pLocalPlayer = pGame ? pGame->FindPlayerPed() : nullptr;
        if (!pLocalPlayer || !pLocalPlayer->IsInVehicle()) return;

        CVehicle* pVeh = nullptr;
        if (pNetGame && pNetGame->GetVehiclePool() && pLocalPlayer->m_pPed) {
                CVehiclePool* pPool = pNetGame->GetVehiclePool();
                VEHICLEID vehID = pPool->FindIDFromGtaPtr(pLocalPlayer->m_pPed->pVehicle);
                if (vehID != INVALID_VEHICLE_ID) pVeh = pPool->GetAt(vehID);
        }
        if (!pVeh || !pVeh->m_pVehicle) return;
        CVehicleGTA* gtaVeh = pVeh->m_pVehicle;

        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 disp = io.DisplaySize;
        const float u = disp.y / 720.0f;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImFont* font = ImGui::GetFont();

        const float r = 92.0f * u;
        const ImVec2 c(disp.x * 0.66f, disp.y - r - 14.0f * u);

        // uzun basma ile reng deyisimi
        const bool inRect = g_mmTouch.x >= c.x - r && g_mmTouch.x <= c.x + r &&
                            g_mmTouch.y >= c.y - r && g_mmTouch.y <= c.y + r;
        if (g_mmTouch.down && inRect) {
                s_hudHolding = true;
                s_hudHoldT += 1.0f / 60.0f;
                if (s_hudHoldT > 0.55f) { s_hudColorIdx = (s_hudColorIdx + 1) % 5; s_hudHoldT = 0.0f; }
        } else {
                s_hudHolding = false;
                s_hudHoldT = 0.0f;
        }
        const ImU32 accent = kHudPalette[s_hudColorIdx];

        // ---- melumat ----
        const float mvx = gtaVeh->m_vecMoveSpeed.x, mvy = gtaVeh->m_vecMoveSpeed.y, mvz = gtaVeh->m_vecMoveSpeed.z;
        const float kmh = sqrtf(mvx * mvx + mvy * mvy + mvz * mvz) * 180.0f;
        const float curSpd = gtaVeh->m_pHandlingData ? gtaVeh->m_pHandlingData->m_transmissionData.m_fCurrentSpeed : 0.0f;

        char gear[4];
        if (kmh < 1.5f)         snprintf(gear, sizeof(gear), "N");
        else if (curSpd < -0.01f) snprintf(gear, sizeof(gear), "R");
        else                    snprintf(gear, sizeof(gear), "%d", gtaVeh->m_nCurrentGear < 1 ? 1 : (int)gtaVeh->m_nCurrentGear);

        const bool eng = gtaVeh->m_nVehicleFlags.bEngineOn != 0;
        const bool lig = gtaVeh->m_nVehicleFlags.bLightsOn != 0;
        const int  lk  = (int)gtaVeh->m_nDoorLock;
        const bool locked = (lk == 2 || lk == 3);

        static float s_dialShown = 0.0f;
        float f = kmh / 270.0f;
        if (f > 1.0f) f = 1.0f;
        s_dialShown += (f - s_dialShown) * 0.2f;

        // yol sayaci (sessiya, metr)
        static CVehicleGTA* s_odoVeh = nullptr;
        static double s_odo = 0.0;
        if (s_odoVeh != gtaVeh) { s_odoVeh = gtaVeh; s_odo = 0.0; }
        s_odo += (double)(kmh / 3.6f) * (double)io.DeltaTime;
        char odo[16];
        snprintf(odo, sizeof(odo), "%07d", (int)(s_odo > 9999999.0 ? 9999999.0 : s_odo));

        auto ctext = [&](float x, float y, float size, ImU32 col, const char* s) {
                const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, s);
                dl->AddText(font, size, ImVec2(x - sz.x * 0.5f, y), col, s);
        };

        // ---- cizim ----
        const float a0 = 3.14159265f * 0.75f;
        const float sweep = 3.14159265f * 1.5f;

        dl->AddCircleFilled(c, r * 1.04f, IM_COL32(8, 10, 16, 170), 48);
        dl->AddCircle(c, r * 1.04f, IM_COL32(255, 255, 255, 28), 48, 1.4f * u);

        // esas qovs
        dl->PathArcTo(c, r * 0.92f, a0, a0 + sweep, 48);
        dl->PathStroke(IM_COL32(70, 76, 92, 200), 0, 5.0f * u);
        // qirmizi zona
        dl->PathArcTo(c, r * 0.92f, a0 + sweep * 0.78f, a0 + sweep, 16);
        dl->PathStroke(IM_COL32(235, 60, 80, 235), 0, 5.0f * u);
        // dolu hisse
        if (s_dialShown > 0.003f) {
                dl->PathArcTo(c, r * 0.92f, a0, a0 + sweep * s_dialShown, 48);
                dl->PathStroke(accent, 0, 5.0f * u);
        }

        // kicik cizgiler
        for (int k = 0; k <= 27; ++k) {
                if (k % 3 == 0) continue;
                const float a = a0 + sweep * (k / 27.0f);
                dl->AddLine(ImVec2(c.x + cosf(a) * r * 0.84f, c.y + sinf(a) * r * 0.84f),
                            ImVec2(c.x + cosf(a) * r * 0.88f, c.y + sinf(a) * r * 0.88f),
                            IM_COL32(150, 154, 168, 180), 1.0f * u);
        }
        // boyuk cizgiler + reqemler (hər biri 30 km/h)
        for (int i = 0; i <= 9; ++i) {
                const float a = a0 + sweep * (i / 9.0f);
                const float ca = cosf(a), sa = sinf(a);
                dl->AddLine(ImVec2(c.x + ca * r * 0.80f, c.y + sa * r * 0.80f),
                            ImVec2(c.x + ca * r * 0.88f, c.y + sa * r * 0.88f),
                            IM_COL32(215, 219, 232, 230), 1.8f * u);
                char lb[8];
                snprintf(lb, sizeof(lb), "%d", i * 30);
                const float ts = 9.5f * u;
                const ImVec2 tsz = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, lb);
                dl->AddText(font, ts, ImVec2(c.x + ca * r * 0.67f - tsz.x * 0.5f, c.y + sa * r * 0.67f - tsz.y * 0.5f),
                            IM_COL32(200, 204, 218, 230), lb);
        }

        // ibre
        const float na = a0 + sweep * s_dialShown;
        dl->AddLine(ImVec2(c.x + cosf(na) * r * 0.20f, c.y + sinf(na) * r * 0.20f),
                    ImVec2(c.x + cosf(na) * r * 0.80f, c.y + sinf(na) * r * 0.80f),
                    IM_COL32(255, 255, 255, 240), 2.4f * u);

        // merkez: pillə (viteS)
        dl->AddCircleFilled(c, r * 0.22f, IM_COL32(14, 16, 24, 240), 28);
        dl->AddCircle(c, r * 0.22f, accent, 28, 1.8f * u);
        ctext(c.x, c.y - r * 0.13f, r * 0.26f, IM_COL32(255, 255, 255, 255), gear);

        // gostericiler (M = motor, F = fara, Q = qapi)
        auto chip = [&](float x, float y, bool on, const char* t) {
                const float cr = 9.0f * u;
                dl->AddCircleFilled(ImVec2(x, y), cr, on ? accent : IM_COL32(60, 64, 76, 210), 20);
                const float ts = 9.0f * u;
                const ImVec2 s = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, t);
                dl->AddText(font, ts, ImVec2(x - s.x * 0.5f, y - s.y * 0.5f),
                            on ? IM_COL32(10, 12, 18, 255) : IM_COL32(170, 174, 186, 255), t);
        };
        chip(c.x - r * 0.36f, c.y - r * 0.50f, eng, "M");
        chip(c.x,             c.y - r * 0.58f, lig, "F");
        chip(c.x + r * 0.36f, c.y - r * 0.50f, !locked, "Q");

        // sur'et
        char sp[8];
        snprintf(sp, sizeof(sp), "%d", (int)(kmh + 0.5f));
        ctext(c.x, c.y + r * 0.22f, r * 0.42f, IM_COL32(255, 255, 255, 255), sp);
        ctext(c.x, c.y + r * 0.66f, r * 0.14f, accent, "KM/H");
        ctext(c.x, c.y + r * 0.80f, r * 0.12f, IM_COL32(190, 194, 208, 220), odo);
}
'''

TOUCH_A = r'''if (g_bHideAllUI && !g_bShowHandlingDlg) {
                // gizli rejim: yalniz CHAT sahesine basanda UI qayidir, qalan toxunuslar alt widget-lere getmir
                const ImVec2 dsz = ImGui::GetIO().DisplaySize;
                const bool hitChat = (pUI && pUI->chat() && pUI->chat()->contains(pos)) ||
                                     (pos.x >= dsz.x * 0.15f && pos.x <= dsz.x * 0.72f && pos.y <= dsz.y * 0.34f);
                if (tt == TOUCH_PUSH && hitChat) {
                        g_bHideAllUI = false;
                        if (pUI && pUI->chat()) pUI->chat()->addDebugMessage("{00FF00}[Client]: Arayuz berpa edildi.");
                }
                s_dlgCapturedEvent = false;
                return;
        }'''

TOUCH_B = r'''if (g_bHideAllUI && !g_bShowHandlingDlg) {
                // gizli rejim: yalniz CHAT sahesine basanda UI qayidir
                const ImVec2 tp((float)x, (float)y);
                const ImVec2 dsz = ImGui::GetIO().DisplaySize;
                const bool hitChat = (pUI && pUI->chat() && pUI->chat()->contains(tp)) ||
                                     (tp.x >= dsz.x * 0.15f && tp.x <= dsz.x * 0.72f && tp.y <= dsz.y * 0.34f);
                if (type == TOUCH_PUSH && hitChat) {
                        g_bHideAllUI = false;
                        if (pUI && pUI->chat()) pUI->chat()->addDebugMessage("{00FF00}[Client]: Arayuz berpa edildi.");
                }
                return true;
        }'''

NEW_SIGNALS = r'''    void RenderSignals(CVehicleGTA* veh)
    {
        if (s_signals.empty() || !veh) return;

        auto it = s_signals.find(veh);
        if (it == s_signals.end()) return;
        if (!BlinkOn()) return;

        auto* pModelInfo = CModelInfo::GetVehicleModelInfo(veh->m_nModelIndex);
        if (!pModelInfo || !pModelInfo->m_pVehicleStruct) return;
        const CVector* dummy = pModelInfo->m_pVehicleStruct->m_avDummyPos;

        const uint8_t mode = it->second;

        for (int side = 0; side < 2; ++side)          // 0 = sol, 1 = sag
        {
            const bool wanted = (mode == VEH_SIGNAL_HAZARD) ||
                                (mode == VEH_SIGNAL_LEFT  && side == 0) ||
                                (mode == VEH_SIGNAL_RIGHT && side == 1);
            if (!wanted) continue;

            for (int end = 0; end < 2; ++end)         // 0 = on, 1 = arxa
            {
                CVector v(dummy[end == 0 ? 0 : 1]);
                if (fabsf(v.x) < 0.05f) continue;     // dummy yoxdur (motosiklet, qayiq ve s.)

                if (!side) v.x = -v.x;                // oyunun oz mentiqi ile eyni

                // lampanin ozunun yerinde qal, yalniz korpusun icinde itmesin deye azca xaricə / on-arxaya cək
                v.x += (v.x >= 0.0f ? 0.03f : -0.03f);
                v.y += (end == 0 ? 0.06f : -0.06f);

                CCoronas::RegisterCorona(
                        (uint32)((uintptr)&veh->m_placement.m_vPosn.y + 40 + end * 2 + side),
                        veh,
                        255, 150, 20, 255,
                        &v,
                        end == 0 ? 0.55f : 0.60f,
                        60.f,
                        eCoronaType::CORONATYPE_HEADLIGHT,
                        eCoronaFlareType::FLARETYPE_NONE,
                        false,
                        false,
                        0,
                        0.0f,
                        false,
                        0.6f,                         // nearClip: koronani kameraya dogru cekir -> korpusun icinde gizlenmir
                        0,
                        15.0f,
                        false,
                        false
                );
            }
        }
    }
'''

# ============================ gui.cpp =======================================
t, crlf = load(GUI)

m = re.search(r"void RenderVehicleHud\(\)\s*\{", t)
if not m:
    die("gui.cpp: 'void RenderVehicleHud() {' tapilmadi")
start = m.start()
end = t.find("void UI::drawList()")
if end == -1 or end < start:
    die("gui.cpp: 'void UI::drawList()' tapilmadi")
t = t[:start] + NEW_HUD + "\n" + t[end:]

pat_a = re.compile(r"if \(g_bHideAllUI && !g_bShowHandlingDlg && tt == TOUCH_PUSH\) \{.*?return;\s*\}", re.S)
if len(pat_a.findall(t)) != 1:
    die("gui.cpp: UI::touchEvent gizletme blokunu tapa bilmedim")
t = pat_a.sub(lambda _m: TOUCH_A, t, count=1)

pat_b = re.compile(r"if \(g_bHideAllUI && !g_bShowHandlingDlg && type == TOUCH_PUSH\) \{.*?return true;\s*\}", re.S)
if len(pat_b.findall(t)) != 1:
    die("gui.cpp: UI::OnTouchEvent gizletme blokunu tapa bilmedim")
t = pat_b.sub(lambda _m: TOUCH_B, t, count=1)

save(GUI, t, crlf)
print("OK  gui.cpp: speedometr + UI gizletme")

# ============================ CVehicleGTA.cpp ===============================
v, crlf2 = load(VEH)
s = v.find("    void RenderSignals(CVehicleGTA* veh)")
if s == -1:
    die("CVehicleGTA.cpp: RenderSignals tapilmadi")
e = v.find("}\n\nvoid CVehicleGTA::InjectHooks()")
if e == -1 or e < s:
    die("CVehicleGTA.cpp: namespace sonu tapilmadi")
v = v[:s] + NEW_SIGNALS + v[e:]
save(VEH, v, crlf2)
print("OK  CVehicleGTA.cpp: sinyal isiqlari")

print("\nHazirdir. Indi:  git diff --stat   sonra  git add -A && git commit -m 'speedo, sinyal, ui gizle' && git push")
