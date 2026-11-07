#!/usr/bin/env python3
# Repo kokunde isle:  python3 apply_patch2.py
#
# 1) SINYAL: lampanin ozu yanib-sonur (ms_lightsOn + m_renderLights), uzen corona defolt SONULUDUR
#    /sinyalayar ters    -> sol/sag terefler ters ise duzelt
#    /sinyalayar korona  -> kohne ayri narinci corona-ni ac/bagla
# 2) NOMRE: Azerbaycan lovhesi (10-AA-123), /mm -> "Nomre" sehifesi + /nomre 10-AA-123 | off
#
# Hamisi yaddasda yoxlanir; bir anchor tapilmasa HEC BIR fayl deyismir.
import os, re, shutil, sys

R = "app/src/main/cpp/samp"
F_GUI   = R + "/gui/gui.cpp"
F_CHAT  = R + "/gui/samp_widgets/chat.cpp"
F_VEH   = R + "/game/Entity/CVehicleGTA.cpp"
F_VCH   = R + "/game/VehControls.h"
F_PATCH = R + "/game/patches.cpp"
F_MTG   = R + "/game/materialtextgenerator.h"
F_AZH   = R + "/game/AzPlate.h"
F_AZC   = R + "/game/AzPlate.cpp"


def load(p):
    if not os.path.exists(p):
        die("fayl yoxdur: " + p)
    t = open(p, "rb").read().decode("utf-8")
    return t.replace("\r\n", "\n"), ("\r\n" in t)


def die(msg):
    print("XETA:", msg)
    print("Hec bir fayl deyismedi.")
    sys.exit(1)


def rep1(t, old, new, name):
    if t.count(old) != 1:
        die("%s: anchor %d dəfə tapildi (1 gozlenirdi): %r" % (name, t.count(old), old[:60]))
    return t.replace(old, new)


def rex1(t, pattern, new, name, flags=0):
    ms = list(re.finditer(pattern, t, flags))
    if len(ms) != 1:
        die("%s: regex %d dəfə tapildi (1 gozlenirdi): %s" % (name, len(ms), pattern[:60]))
    m = ms[0]
    return t[:m.start()] + (new(m) if callable(new) else new) + t[m.end():]


# =============================================================================
#  C++ parcalari
# =============================================================================
AZ_H = r'''#pragma once
// Azerbaycan nomre lovhesi (10-AA-123). Yalniz YERLI ekranda gorunur.
#include <string>

struct CVehicleGTA;

namespace AzPlate
{
    // "10aa123" / "10-AA-123" / "10 aa 123"  ->  "10-AA-123". Format duz deyilse false.
    bool Normalize(const std::string& in, std::string& out);

    // Lovheni masina teyin edir. Masinda lovhe materiali (carplate/nomer) tapilmasa false.
    bool Set(CVehicleGTA* veh, const std::string& normalized);
    void Clear(CVehicleGTA* veh);
    bool Get(CVehicleGTA* veh, std::string& out);

    // UI::render-den cagirilir, ImGui kadrindan EVVEL (tekstura yaradilir)
    void ProcessPending();

    // CAutomobile::CustomCarPlate_BeforeRenderingStart/AfterRenderingStop evezlenir
    void InstallHooks();
}
'''

AZ_CPP = r'''// Azerbaycan nomre lovhesi: MaterialTextGenerator ile tekstura cizilir, masinin render-i
// zamani plate materialinin teksturu muveqqeti evez olunur (material model uzre ORTAQDIR).
#include "../main.h"
#include "game.h"
#include "../gui/gui.h"
#include "RW/RenderWare.h"
#include "RW/rpworld.h"
#include "Scene.h"
#include "VisibilityPlugins.h"
#include "Entity/CVehicleGTA.h"
#include "../vendor/armhook/patch.h"
#include "AzPlate.h"

#include <cctype>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

extern UI* pUI;

// ----------------------------------------------------------------------------
//  MaterialTextGenerator-un lovhe metodlari (header-de elan olunub)
// ----------------------------------------------------------------------------
void MaterialTextGenerator::RenderPlate(const char* text, float w, float h)
{
    if (!pUI) return;

    ImGui::NewFrame();

    auto* r = pUI->renderer();
    const ImColor cBlack(15, 15, 15, 255);
    const ImColor cWhite(250, 250, 250, 255);
    const ImColor cBlue(0, 86, 170, 255);
    const ImColor cSky(0, 174, 239, 255);
    const ImColor cRed(224, 60, 49, 255);
    const ImColor cGreen(0, 150, 63, 255);

    r->drawRect(ImVec2(0.0f, 0.0f), ImVec2(w, h), cBlack, true);
    const float b = h * 0.045f;
    r->drawRect(ImVec2(b, b), ImVec2(w - 2.0f * b, h - 2.0f * b), cWhite, true);

    const float bandW = w * 0.14f;
    r->drawRect(ImVec2(b, b), ImVec2(bandW, h - 2.0f * b), cBlue, true);

    // bayraq
    const float fw = bandW * 0.62f, fh = h * 0.085f;
    const float fx = b + (bandW - fw) * 0.5f, fy = h * 0.17f;
    r->drawRect(ImVec2(fx, fy), ImVec2(fw, fh), cSky, true);
    r->drawRect(ImVec2(fx, fy + fh), ImVec2(fw, fh), cRed, true);
    r->drawRect(ImVec2(fx, fy + 2.0f * fh), ImVec2(fw, fh), cGreen, true);

    // "AZ"
    const int azSize = (int)(h * 0.30f);
    const ImVec2 azSz = r->calculateTextSize("AZ", azSize);
    r->drawText(ImVec2(b + (bandW - azSz.x) * 0.5f, h * 0.55f), cWhite, "AZ", false, azSize);

    // esas yazi (sigmasa kicildilir)
    const float areaX = b + bandW;
    const float areaW = w - 2.0f * b - bandW;
    int txtSize = (int)(h * 0.62f);
    ImVec2 tsz = r->calculateTextSize(text, txtSize);
    if (tsz.x > areaW * 0.94f && tsz.x > 1.0f) {
        txtSize = (int)((float)txtSize * (areaW * 0.94f) / tsz.x);
        if (txtSize < 8) txtSize = 8;
        tsz = r->calculateTextSize(text, txtSize);
    }
    r->drawText(ImVec2(areaX + (areaW - tsz.x) * 0.5f, (h - tsz.y) * 0.5f), cBlack, text, false, txtSize);

    ImGui::EndFrame();
    ImGui::Render();
    pUI->renderDrawData(ImGui::GetDrawData());
}

RwTexture* MaterialTextGenerator::GeneratePlate(const char* text)
{
    if (!m_camera || !pUI) return nullptr;

    const int width = 512, height = 128;
    RwRaster* raster = RwRasterCreate(width, height, 32, rwRASTERFORMAT8888 | rwRASTERTYPECAMERATEXTURE);
    RwTexture* tex = RwTextureCreate(raster);
    if (!raster || !tex) return nullptr;

    m_camera->frameBuffer = raster;
    CVisibilityPlugins::SetRenderWareCamera(m_camera);

    uint32_t white = 0xFFFFFFFFu;
    RwCameraClear(m_camera, reinterpret_cast<RwRGBA*>(&white), 3);
    RwCameraBeginUpdate((RwCamera*)m_camera);
    DefinedState();
    RenderPlate(text, (float)width, (float)height);
    RwCameraEndUpdate((RwCamera*)m_camera);

    return tex;
}

// ----------------------------------------------------------------------------
namespace
{
    struct Entry
    {
        uint32_t    creationTime = 0;
        std::string text;
        RwTexture*  tex   = nullptr;
        bool        dirty = true;
    };

    std::unordered_map<CVehicleGTA*, Entry> g_entries;
    std::mutex                              g_mtx;
    MaterialTextGenerator*                  g_gen = nullptr;

    std::vector<RpMaterial*>* g_collect = nullptr;

    bool IsPlateName(const char* n)
    {
        if (!n) return false;
        char b[16];
        size_t i = 0;
        for (; n[i] && i < 15; ++i) b[i] = (char)tolower((unsigned char)n[i]);
        b[i] = 0;
        return strncmp(b, "nomer", 5) == 0 || strncmp(b, "carplate", 8) == 0;
    }

    RpMaterial* MatCB(RpMaterial* mat, void*)
    {
        if (mat && mat->texture && g_collect && IsPlateName(mat->texture->name))
            g_collect->push_back(mat);
        return mat;
    }

    RpAtomic* AtomCB(RpAtomic* atomic, void*)
    {
        auto geo = RpAtomicGetGeometry(atomic);
        if (geo) RpGeometryForAllMaterials(geo, MatCB, nullptr);
        return atomic;
    }

    void CollectPlateMaterials(CVehicleGTA* veh, std::vector<RpMaterial*>& out)
    {
        out.clear();
        if (!veh || !veh->m_pRwClump) return;
        g_collect = &out;
        RpClumpForAllAtomics(veh->m_pRwClump, AtomCB, nullptr);
        g_collect = nullptr;
    }

    std::vector<std::pair<RpMaterial*, RwTexture*>> s_saved;

    void Before_hook(CVehicleGTA* thiz, void* /*modelInfo*/)
    {
        s_saved.clear();
        RwTexture* tex = nullptr;
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            if (g_entries.empty() || !thiz) return;
            auto it = g_entries.find(thiz);
            if (it == g_entries.end()) return;
            if (it->second.creationTime != thiz->m_nCreationTime) { g_entries.erase(it); return; }
            tex = it->second.tex;
        }
        if (!tex) return;

        std::vector<RpMaterial*> mats;
        CollectPlateMaterials(thiz, mats);
        for (RpMaterial* m : mats) {
            s_saved.emplace_back(m, m->texture);
            m->texture = tex;
        }
    }

    void After_hook(CVehicleGTA* /*thiz*/, void* /*modelInfo*/)
    {
        for (auto& p : s_saved) p.first->texture = p.second;
        s_saved.clear();
    }

    inline bool Dig(char c) { return c >= '0' && c <= '9'; }
    inline bool Let(char c) { return c >= 'A' && c <= 'Z'; }
}

namespace AzPlate
{
    bool Normalize(const std::string& in, std::string& out)
    {
        std::string s;
        for (char c : in) {
            if (c == '-' || c == ' ' || c == '_') continue;
            s.push_back((char)toupper((unsigned char)c));
        }
        if (s.size() != 7) return false;
        if (!Dig(s[0]) || !Dig(s[1])) return false;
        if (!Let(s[2]) || !Let(s[3])) return false;
        if (!Dig(s[4]) || !Dig(s[5]) || !Dig(s[6])) return false;
        out = s.substr(0, 2) + "-" + s.substr(2, 2) + "-" + s.substr(4, 3);
        return true;
    }

    bool Set(CVehicleGTA* veh, const std::string& normalized)
    {
        if (!veh) return false;

        std::vector<RpMaterial*> mats;
        CollectPlateMaterials(veh, mats);
        if (mats.empty()) return false;

        std::lock_guard<std::mutex> lk(g_mtx);
        Entry& e = g_entries[veh];
        if (e.creationTime != veh->m_nCreationTime) {
            e = Entry();
            e.creationTime = veh->m_nCreationTime;
        }
        e.text  = normalized;
        e.dirty = true;       // kohne tekstura yenisi hazir olana qeder qalir
        return true;
    }

    void Clear(CVehicleGTA* veh)
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_entries.erase(veh);
    }

    bool Get(CVehicleGTA* veh, std::string& out)
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        auto it = g_entries.find(veh);
        if (it == g_entries.end()) return false;
        if (it->second.creationTime != veh->m_nCreationTime) return false;
        out = it->second.text;
        return true;
    }

    void ProcessPending()
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        for (auto& kv : g_entries) {
            Entry& e = kv.second;
            if (!e.dirty) continue;
            e.dirty = false;      // bir defe cehd edilir
            if (!g_gen) g_gen = new MaterialTextGenerator();
            RwTexture* t = g_gen->GeneratePlate(e.text.c_str());
            if (t) e.tex = t;     // (kohne tekstura azad edilmir: kicik sizinti, yalniz lovhe deyisende)
        }
    }

    void InstallHooks()
    {
        CHook::Redirect("_ZN11CAutomobile35CustomCarPlate_BeforeRenderingStartEP17CVehicleModelInfo", &Before_hook);
        CHook::Redirect("_ZN11CAutomobile33CustomCarPlate_AfterRenderingStopEP17CVehicleModelInfo", &After_hook);
    }
}
'''

PLATE_PREVIEW = r'''// ---- AZ nomre lovhesi onizlemesi (/mm -> Nomre) ----
static void PlatePreview(const mm::Rc& r, const char* text)
{
        using namespace mm;
        Fill(r, Col(14, 14, 14, 255), S(10.0f));
        const Rc in = Shrink(r, S(3.5f));
        Fill(in, Col(248, 248, 248, 255), S(8.0f));
        const float bandW = in.w * 0.14f;
        Fill(Rc{ in.x, in.y, bandW, in.h }, Col(0, 86, 170, 255), S(8.0f));
        const float fw = bandW * 0.62f, fh = in.h * 0.085f;
        const float fx = in.x + (bandW - fw) * 0.5f, fy = in.y + in.h * 0.17f;
        Fill(Rc{ fx, fy, fw, fh },               Col(0, 174, 239, 255), 0.0f);
        Fill(Rc{ fx, fy + fh, fw, fh },          Col(224, 60, 49, 255), 0.0f);
        Fill(Rc{ fx, fy + 2.0f * fh, fw, fh },   Col(0, 150, 63, 255),  0.0f);
        TxtC(in.h * 0.28f, Rc{ in.x, in.y + in.h * 0.52f, bandW, in.h * 0.36f }, Col(255, 255, 255, 255), "AZ", true);
        TxtC(in.h * 0.56f, Rc{ in.x + bandW, in.y, in.w - bandW, in.h }, Col(12, 12, 12, 255), text, true);
}

'''

PLATE_PAGE = r'''else if (s_page == 6)    // NOMRE (Azerbaycan)
                {
                        static int  s_plReg = 10, s_plL1 = 0, s_plL2 = 0, s_plNum = 123;
                        static CVehicleGTA* s_plVeh = nullptr;
                        static char  s_plMsg[96] = "";
                        static float s_plMsgT = 0.0f;

                        if (gtaVeh != s_plVeh) {
                                s_plVeh = gtaVeh;
                                std::string cur;
                                if (gtaVeh && AzPlate::Get(gtaVeh, cur) && cur.size() == 9) {
                                        s_plReg = (cur[0] - '0') * 10 + (cur[1] - '0');
                                        s_plL1  = cur[3] - 'A';
                                        s_plL2  = cur[4] - 'A';
                                        s_plNum = (cur[6] - '0') * 100 + (cur[7] - '0') * 10 + (cur[8] - '0');
                                }
                        }
                        s_plMsgT = Clamp(s_plMsgT - dt / 3.0f, 0.0f, 1.0f);

                        auto wrap = [](int v, int n) { return ((v % n) + n) % n; };

                        // [-10][-1] deyer [+1][+10]  (big)   ve ya   [-] deyer [+]
                        auto stepper = [&](int idBase, const Rc& row, const char* label, const char* val, bool big) -> int {
                                Fill(row, Col(27, 30, 40, 255), S(10.0f));
                                Txt(S(15.0f), row.x + S(14.0f), row.y + (row.h - S(15.0f)) * 0.5f, Col(200, 206, 220), label);

                                const int   side = big ? 2 : 1;
                                const int   nb = side * 2;
                                const float bw = S(44.0f), bh = S(34.0f), gap = S(6.0f), valW = S(72.0f);
                                const float total = nb * bw + nb * gap + valW;
                                float x = row.x + row.w - S(12.0f) - total;
                                const float by = row.y + (row.h - bh) * 0.5f;
                                static const char* lbBig[4]   = { "-10", "-1", "+1", "+10" };
                                static const char* lbSmall[2] = { "-", "+" };
                                static const int   dBig[4]    = { -10, -1, 1, 10 };
                                static const int   dSmall[2]  = { -1, 1 };
                                int delta = 0;
                                for (int k = 0; k < nb; ++k) {
                                        if (k == side) {
                                                TxtC(S(21.0f), Rc{ x, by, valW, bh }, Accent(0.0f), val, true);
                                                x += valW + gap;
                                        }
                                        const Rc b{ x, by, bw, bh };
                                        const int id = idBase + k;
                                        if (Tap(id, b, canInput)) delta = big ? dBig[k] : dSmall[k];
                                        const float p = Prs(id, dt);
                                        Fill(Shrink(b, S(1.2f) * p), Col(255, 255, 255, 16 + (int)(34.0f * p)), S(8.0f));
                                        TxtC(S(15.0f), b, Col(226, 230, 240), big ? lbBig[k] : lbSmall[k], true);
                                        x += bw + gap;
                                }
                                return delta;
                        };

                        char plate[16];
                        snprintf(plate, sizeof(plate), "%02d-%c%c-%03d", s_plReg, 'A' + s_plL1, 'A' + s_plL2, s_plNum);
                        PlatePreview(Rc{ cx0 + (cw - S(420.0f)) * 0.5f, P.y + S(124.0f), S(420.0f), S(105.0f) }, plate);

                        char sReg[8], sL1[4], sL2[4], sNum[8];
                        snprintf(sReg, sizeof(sReg), "%02d", s_plReg);
                        snprintf(sL1, sizeof(sL1), "%c", 'A' + s_plL1);
                        snprintf(sL2, sizeof(sL2), "%c", 'A' + s_plL2);
                        snprintf(sNum, sizeof(sNum), "%03d", s_plNum);

                        s_plReg = wrap(s_plReg + stepper(80, Rc{ cx0, P.y + S(244.0f), cw, S(46.0f) }, "Region kodu", sReg, false), 100);
                        s_plL1  = wrap(s_plL1  + stepper(82, Rc{ cx0, P.y + S(296.0f), cw * 0.49f, S(46.0f) }, "Hərf 1", sL1, false), 26);
                        s_plL2  = wrap(s_plL2  + stepper(84, Rc{ cx0 + cw * 0.51f, P.y + S(296.0f), cw * 0.49f, S(46.0f) }, "Hərf 2", sL2, false), 26);
                        s_plNum = wrap(s_plNum + stepper(86, Rc{ cx0, P.y + S(348.0f), cw, S(46.0f) }, "Rəqəmlər", sNum, true), 1000);

                        const Rc bApply{ cx0, P.y + S(412.0f), S(190.0f), S(44.0f) };
                        const Rc bRand { cx0 + S(204.0f), P.y + S(412.0f), S(190.0f), S(44.0f) };
                        const Rc bOff  { cx0 + S(408.0f), P.y + S(412.0f), S(190.0f), S(44.0f) };

                        if (Tap(90, bApply, canInput) && gtaVeh) {
                                std::string norm;
                                AzPlate::Normalize(plate, norm);
                                if (AzPlate::Set(gtaVeh, norm)) snprintf(s_plMsg, sizeof(s_plMsg), "Tətbiq edildi: %s", plate);
                                else snprintf(s_plMsg, sizeof(s_plMsg), "Bu maşında nömrə lövhəsi materialı tapılmadı");
                                s_plMsgT = 1.0f;
                        }
                        if (Tap(91, bRand, canInput)) {
                                static const int kReg[4] = { 10, 90, 99, 77 };
                                s_plReg = kReg[rand() % 4];
                                s_plL1 = rand() % 26;
                                s_plL2 = rand() % 26;
                                s_plNum = rand() % 1000;
                        }
                        if (Tap(92, bOff, canInput) && gtaVeh) {
                                AzPlate::Clear(gtaVeh);
                                snprintf(s_plMsg, sizeof(s_plMsg), "Standart nömrəyə qayıdıldı");
                                s_plMsgT = 1.0f;
                        }

                        auto btn = [&](int id, const Rc& b, const char* label, bool primary) {
                                const float p = Prs(id, dt);
                                const Rc d = Shrink(b, S(1.5f) * p);
                                if (primary) {
                                        Fill(d, Accent(0.0f, 190 + (int)(40.0f * p)), S(9.0f));
                                } else {
                                        Fill(d, Col(255, 255, 255, 14 + (int)(30.0f * p)), S(9.0f));
                                        Stroke(d, Col(255, 255, 255, 40), S(9.0f), S(1.2f));
                                }
                                TxtC(S(16.0f), d, Col(240, 244, 252), label, true);
                        };
                        btn(90, bApply, "Tətbiq et", true);
                        btn(91, bRand, "Təsadüfi", false);
                        btn(92, bOff, "Standart", false);

                        if (s_plMsgT > 0.01f)
                                Txt(S(15.0f), cx0, P.y + S(468.0f), Col(120, 215, 160, (int)(255.0f * s_plMsgT)), s_plMsg);
                        Txt(S(12.0f), cx0, P.y + S(500.0f), Col(124, 132, 152), "Qeyd: nömrə yalnız sizin ekranınızda görünür.");
                }
                '''

CHAT_CMDS = r'''if (cmd == "/sinyalayar")
        {
                std::string a;
                ss >> a;
                std::transform(a.begin(), a.end(), a.begin(), ::tolower);
                if (a == "ters") {
                        VehControls::g_sigSwapSides = !VehControls::g_sigSwapSides;
                        pUI->chat()->addDebugMessage("{00FF00}[Client]: Sinyal terefleri: %s", VehControls::g_sigSwapSides ? "TERS" : "normal");
                } else if (a == "korona") {
                        VehControls::g_sigCorona = !VehControls::g_sigCorona;
                        pUI->chat()->addDebugMessage("{00FF00}[Client]: Sinyal koronasi: %s", VehControls::g_sigCorona ? "aciq" : "bagli");
                } else {
                        pUI->chat()->addDebugMessage("{FF0000}[Client]: Istifade: /sinyalayar [ters|korona]");
                }
                return true;
        }

        if (cmd == "/nomre")
        {
                CPlayerPed* pLocalPlayer = pGame ? pGame->FindPlayerPed() : nullptr;
                if (!pLocalPlayer || !pLocalPlayer->IsInVehicle()) {
                        pUI->chat()->addDebugMessage("{FF0000}[Client]: Xeta: Avtomobilde deyilsiniz!");
                        return true;
                }

                CVehicle* pVeh = nullptr;
                if (pNetGame && pNetGame->GetVehiclePool() && pLocalPlayer->m_pPed) {
                        CVehiclePool* pPool = pNetGame->GetVehiclePool();
                        VEHICLEID vehID = pPool->FindIDFromGtaPtr(pLocalPlayer->m_pPed->pVehicle);
                        if (vehID != INVALID_VEHICLE_ID) pVeh = pPool->GetAt(vehID);
                }
                if (!pVeh || !pVeh->m_pVehicle) {
                        pUI->chat()->addDebugMessage("{FF0000}[Client]: Avtomobil tapilmadi!");
                        return true;
                }

                std::string arg;
                ss >> arg;
                std::string low = arg;
                std::transform(low.begin(), low.end(), low.begin(), ::tolower);

                if (low == "off" || low == "0") {
                        AzPlate::Clear(pVeh->m_pVehicle);
                        pUI->chat()->addDebugMessage("{00FF00}[Client]: Standart nomreye qayidildi.");
                        return true;
                }

                std::string norm;
                if (!AzPlate::Normalize(arg, norm)) {
                        pUI->chat()->addDebugMessage("{FF0000}[Client]: Istifade: /nomre 10-AA-123  (ve ya /nomre off)");
                        return true;
                }
                if (AzPlate::Set(pVeh->m_pVehicle, norm))
                        pUI->chat()->addDebugMessage("{00FF00}[Client]: Nomre: %s", norm.c_str());
                else
                        pUI->chat()->addDebugMessage("{FF0000}[Client]: Bu masinda nomre lovhesi materiali tapilmadi (carplate/nomer).");
                return true;
        }

        if (cmd == "/sinyal")
        {'''

VEH_FUNCS = r'''    // ms_lightsOn / m_renderLights indeksleri: 0 = surucu terefi on, 1 = sernisin terefi on,
    //                                           2 = surucu terefi arxa, 3 = sernisin terefi arxa
    static void ComputeSides(uint8_t mode, bool& left, bool& right)
    {
        left  = (mode == VEH_SIGNAL_HAZARD) || (mode == VEH_SIGNAL_LEFT);
        right = (mode == VEH_SIGNAL_HAZARD) || (mode == VEH_SIGNAL_RIGHT);
        if (g_sigSwapSides) std::swap(left, right);
    }

    // DoVehicleLights hook-undan (PreRender): oyunun oz "bu lampa yanir" bayraqlari
    void ApplyRenderLightBits(CVehicleGTA* veh)
    {
        if (s_signals.empty() || !veh) return;
        auto it = s_signals.find(veh);
        if (it == s_signals.end()) return;

        bool l, r;
        ComputeSides(it->second, l, r);
        const uint8_t on = BlinkOn() ? 1 : 0;
        if (l) { veh->m_renderLights.m_bLeftFront  = on; veh->m_renderLights.m_bLeftRear  = on; }
        if (r) { veh->m_renderLights.m_bRightFront = on; veh->m_renderLights.m_bRightRear = on; }
    }

    // CVehicleModelInfo::SetupLightFlags-dan SONRA: render zamani lampa materiallarinin yanma massivi
    void ApplyLampArray(CVehicleGTA* veh)
    {
#if !VER_x32
        if (s_signals.empty() || !veh) return;
        auto it = s_signals.find(veh);
        if (it == s_signals.end()) return;

        bool l, r;
        ComputeSides(it->second, l, r);
        const uint8_t on = BlinkOn() ? 1 : 0;
        uint8_t* lamps = reinterpret_cast<uint8_t*>(g_libGTASA + 0xB8DB40);   // CVehicleModelInfo::ms_lightsOn (libGTASA nm)
        if (l) { lamps[0] = on; lamps[2] = on; }
        if (r) { lamps[1] = on; lamps[3] = on; }
#else
        (void)veh;
#endif
    }

    void RenderSignals(CVehicleGTA* veh)'''

VEH_HOOKFN = r'''// Render zamani lampa materiallarinin yanma massivini (ms_lightsOn) sinyala gore deyisir
void (*CVehicleModelInfo__SetupLightFlags)(CVehicleGTA* veh);
void CVehicleModelInfo__SetupLightFlags_hook(CVehicleGTA* veh)
{
    CVehicleModelInfo__SetupLightFlags(veh);
    VehControls::ApplyLampArray(veh);
}

void CVehicleGTA::InjectHooks() {'''

# =============================================================================
#  Fayllari yaddasda deyis
# =============================================================================
gui, crlf_gui = load(F_GUI)
chat, crlf_chat = load(F_CHAT)
veh, crlf_veh = load(F_VEH)
vch, crlf_vch = load(F_VCH)
pat, crlf_pat = load(F_PATCH)
mtg, crlf_mtg = load(F_MTG)

# ---- VehControls.h ----
vch = rex1(vch, r"RenderSignals\(CVehicleGTA\* veh\);[^\n]*\n",
           lambda m: m.group(0) +
           "\t// /sinyalayar ters|korona ile deyisdirilir\n"
           "\tinline bool g_sigSwapSides = false;   // sol/sag ters olarsa\n"
           "\tinline bool g_sigCorona    = false;   // kohne ayri narinci corona\n"
           "\tvoid    ApplyRenderLightBits(CVehicleGTA* veh);\n"
           "\tvoid    ApplyLampArray(CVehicleGTA* veh);\n",
           "VehControls.h")

# ---- CVehicleGTA.cpp ----
veh = rep1(veh, '#include "game/VehControls.h"', '#include "game/VehControls.h"\n#include "game/AzPlate.h"', "CVehicleGTA include")
veh = rep1(veh, "#include <cmath>", "#include <cmath>\n#include <utility>", "CVehicleGTA <cmath>")
veh = rep1(veh, "    VehControls::RenderSignals(thiz);",
           "    VehControls::ApplyRenderLightBits(thiz);\n    VehControls::RenderSignals(thiz);", "DoVehicleLights hook")
veh = rep1(veh, "        if (!BlinkOn()) return;",
           "        if (!g_sigCorona) return;      // lampanin ozu yanir; ayri corona yalniz /sinyalayar korona ile\n        if (!BlinkOn()) return;",
           "RenderSignals BlinkOn")
veh = rep1(veh, "    void RenderSignals(CVehicleGTA* veh)", VEH_FUNCS, "RenderSignals def")
veh = rep1(veh, "void CVehicleGTA::InjectHooks() {", VEH_HOOKFN, "InjectHooks def")
veh = rep1(veh,
           'CHook::Redirect("_ZN8CVehicle22GetVehicleLightsStatusEv", &CVehicle__GetVehicleLightsStatus_hook);',
           'CHook::Redirect("_ZN8CVehicle22GetVehicleLightsStatusEv", &CVehicle__GetVehicleLightsStatus_hook);\n'
           '    CHook::InlineHook("_ZN17CVehicleModelInfo15SetupLightFlagsEP8CVehicle", &CVehicleModelInfo__SetupLightFlags_hook, &CVehicleModelInfo__SetupLightFlags);\n'
           '    AzPlate::InstallHooks();',
           "InjectHooks install")

# ---- patches.cpp ----
pat = rex1(pat, r"#define ENABLE_CUSTOM_PLATE 0", "#define ENABLE_CUSTOM_PLATE 1", "patches.cpp")

# ---- materialtextgenerator.h ----
mtg = rex1(mtg, r"RwTexture\*\s+Generate\([^;]*\);",
           lambda m: m.group(0) +
           "\n\n    // Azerbaycan nomre lovhesi (AzPlate.cpp-de tanimlanir)\n"
           "    RwTexture* GeneratePlate(const char* text);\n"
           "    void RenderPlate(const char* text, float w, float h);",
           "materialtextgenerator.h")

# ---- chat.cpp ----
chat = rep1(chat, '#include "../../game/VehControls.h"',
            '#include "../../game/VehControls.h"\n#include "../../game/AzPlate.h"', "chat include")
chat = rex1(chat, r'if \(cmd == "/sinyal"\)\s*\{', CHAT_CMDS, "chat /sinyal anchor")

# ---- gui.cpp ----
gui = rep1(gui, '#include "../game/VehControls.h"', '#include "../game/VehControls.h"\n#include "../game/AzPlate.h"', "gui include")
gui = rex1(gui, r"void UI::render\(\)\s*\{\s*ProcessTouchQueue\(\);[^\n]*\n",
           lambda m: m.group(0) + "\n        AzPlate::ProcessPending();   // nomre teksturalari (ImGui kadrindan evvel)\n",
           "UI::render")
gui = rex1(gui, r"static const char\* kPages\[6\] = \{(.*?)\};",
           lambda m: 'static const char* kPages[7] = {' + m.group(1) + ', "Nömrə" };', "kPages")
gui = rex1(gui, r"static const char\* kSubs\[6\]\s*= \{(.*?)\};",
           lambda m: 'static const char* kSubs[7]  = {' + m.group(1) + ', "Azərbaycan nömrə lövhəsi (10-AA-123)" };', "kSubs")
gui = rep1(gui, "for (int i = 0; i < 6; ++i)", "for (int i = 0; i < 7; ++i)", "nav loop")
gui = rex1(gui, r"else\s+// HANDLING SEHIFELERI", lambda m: PLATE_PAGE + "else                      // HANDLING SEHIFELERI", "plate page")
gui = rex1(gui, r"void RenderHandlingDialog\(\)\s*\{", lambda m: PLATE_PREVIEW + m.group(0), "plate preview")

# =============================================================================
#  Hamisi OK -> yaz
# =============================================================================
def save(p, t, crlf):
    shutil.copyfile(p, p + ".bak2")
    if crlf:
        t = t.replace("\n", "\r\n")
    open(p, "wb").write(t.encode("utf-8"))


save(F_GUI, gui, crlf_gui)
save(F_CHAT, chat, crlf_chat)
save(F_VEH, veh, crlf_veh)
save(F_VCH, vch, crlf_vch)
save(F_PATCH, pat, crlf_pat)
save(F_MTG, mtg, crlf_mtg)
open(F_AZH, "w", encoding="utf-8", newline="\n").write(AZ_H)
open(F_AZC, "w", encoding="utf-8", newline="\n").write(AZ_CPP)

print("OK  gui.cpp, chat.cpp, CVehicleGTA.cpp, VehControls.h, patches.cpp, materialtextgenerator.h")
print("YENI  game/AzPlate.h, game/AzPlate.cpp")

# CMake GLOB yoxlamasi
found = False
for root, dirs, files in os.walk("app/src/main/cpp"):
    dirs[:] = [d for d in dirs if d != "vendor"]
    if "CMakeLists.txt" in files:
        txt = open(os.path.join(root, "CMakeLists.txt"), encoding="utf-8", errors="ignore").read()
        if "GLOB" in txt:
            found = True
            print("CMake GLOB tapildi:", os.path.join(root, "CMakeLists.txt"))
            break
if not found:
    print("DIQQET: CMake-de GLOB tapilmadi. game/AzPlate.cpp-ni CMakeLists.txt-e ELAVE ET.")

print("\nIndi:  git add -A && git commit -m 'sinyal lampa, AZ nomre' && git push")
