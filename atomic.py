#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_v7.py  --  layihenin kokunde isle:   python3 apply_v7.py
Evvel HAMISI yoxlanilir; bir anchor belə tapilmasa HEC BIR FAYL deyismir (yari-yari patch olmur).
Yedek: <fayl>.bak_v7

Edir:
  1) Detallarin sokulmesi  (VehDetach.h/.cpp + render hook)                    [yeni]
  2) /mm yeniden: Maşın | Handling | Tuning | Spidometr
        Tuning alt bolmeleri: Asma | Komponent | İşıq | Nömrə | Detallar
  3) Handling: sade UI, - / + (basili saxla), deyere toxun -> menyu daxili reqem paneli (chata KECMIR)
  4) Handling backend: kutle/maxspeed(vitesler)/asma duzgun tetbiq olunur
  5) Nitro / Hidravlika / Stereo / disk / reng / istenilen komponent ID  (AddComponent yeniden yazildi)
  6) Ag-qara-boz tema (Col mono, Accent boz)
"""
import os, re, sys, shutil

ROOT = os.environ.get("ROOT", os.getcwd())
SAMP = os.path.join(ROOT, "app", "src", "main", "cpp", "samp")
TAG = "MM_V7"


class PatchError(Exception):
    pass


def locate(suffix):
    hits = []
    for d, _, fs in os.walk(SAMP):
        for f in fs:
            p = os.path.join(d, f)
            if p.replace("\\", "/").endswith(suffix):
                hits.append(p)
    if len(hits) != 1:
        raise PatchError("fayl tapilmadi / birden coxdur: %s -> %s" % (suffix, hits))
    return hits[0]


def read(p):
    with open(p, "r", encoding="utf-8", newline="") as f:
        return f.read()


def ws_re(anchor):
    toks = anchor.split()
    return re.compile(r"\s+".join(re.escape(t) for t in toks))


def find1(text, anchor, label):
    ms = list(ws_re(anchor).finditer(text))
    if len(ms) != 1:
        raise PatchError("[%s] anchor %d dene tapildi (1 olmalidir): %r" % (label, len(ms), anchor[:70]))
    return ms[0]


def replace1(text, anchor, new, label):
    m = find1(text, anchor, label)
    return text[:m.start()] + new + text[m.end():]


def insert_before(text, anchor, new, label):
    m = find1(text, anchor, label)
    return text[:m.start()] + new + text[m.start():]


def replace_between(text, a, b, new, label):
    ma = find1(text, a, label + ":bas")
    mb = ws_re(b).search(text, ma.end())
    if not mb:
        raise PatchError("[%s] son anchor tapilmadi: %r" % (label, b[:70]))
    return text[:ma.start()] + new + text[mb.start():]



# =====================================================================================
#  C++ parcalari
# =====================================================================================
GUI_COL = r'''static inline ImU32 Col(int r, int g, int b, int a = 255)
        {   // MM_V7: ag-qara-boz tema -> az doymus (boz-mavi) tonlar saf boza cevrilir; real rengler (qirmizi, yasil...) qalir
                const int mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
                if (mx - mn <= 40) { const int l = (r * 30 + g * 59 + b * 11) / 100; r = g = b = l; }
                return IM_COL32(r, g, b, (int)((float)a * s_A));
        }
        // dəqiq rəng (swatch / önizləmə) - mono süzgəcsiz
        static inline ImU32 ColT(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, (int)((float)a * s_A)); }
        static inline ImU32 Accent(float /*istifade olunmur*/, int a = 255) { return Col(128, 128, 128, a); }'''

GUI_SMALLBTN = r'''Fill(d, primary ? Col(238, 238, 238, 225 + (int)(30.0f * p)) : Col(255, 255, 255, 20 + (int)(30.0f * p)), S(12.0f));
        TxtC(S(15.0f), d, primary ? Col(12, 12, 12) : Col(244, 244, 244), label, true);'''

GUI_TABS = r'''// segmentli idareetme (MM_V7: 4 esas bolme + Tuning alt bolmeleri)
    static const char* kTabs[4] = { "Maşın", "Handling", "Tuning", "Spidometr" };
    static const char* kSubs[5] = { "Asma", "Komponent", "İşıq", "Nömrə", "Detallar" };
    static int   s_sub = 0;
    static float s_subPill = 0.0f;
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
            TxtC(S(16.0f), ti, on ? Col(248, 248, 248) : Col(150, 150, 150), kTabs[i], on);
        }
        if (s_tab == 2) {
            const Rc sg{ P.x + S(24.0f), P.y + S(110.0f), P.w - S(48.0f), S(30.0f) };
            Fill(sg, Col(255, 255, 255, 12), S(10.0f));
            const float sw = sg.w / 5.0f;
            s_subPill = Smooth(s_subPill, (float)s_sub, dt, 16.0f);
            Fill(Rc{ sg.x + S(2.0f) + s_subPill * sw, sg.y + S(2.0f), sw - S(4.0f), sg.h - S(4.0f) }, Col(255, 255, 255, 44), S(8.0f));
            for (int j = 0; j < 5; ++j) {
                const Rc tj{ sg.x + j * sw, sg.y, sw, sg.h };
                if (Tap(200 + j, tj, canInput) && s_sub != j) {
                    s_sub = j;
                    s_tabAnim = 0.0f;
                    s_tabDir = 1.0f;
                }
                TxtC(S(14.0f), tj, (s_sub == j) ? Col(248, 248, 248) : Col(150, 150, 150), kSubs[j], s_sub == j);
            }
        }
    }
    // bolme -> sehife:  0 Masin, 1 Handling, 3 Asma, 6 Komponent, 2 Isiq, 4 Nomre, 7 Detallar, 5 Spidometr
    static const int kSubPage[5] = { 3, 6, 2, 4, 7 };
    const int pg = (s_tab == 2) ? kSubPage[s_sub] : ((s_tab == 3) ? 5 : s_tab);

    '''

GUI_HANDLING = r'''else if (s_tab == 1)                                    // HANDLING (MM_V7: sade, menyu daxili reqem)
    {
        mm3::Ctx cc{ cx0, y0, cw, dt, inputPage, noVeh, pVeh, gtaVeh, P };
        mm3::PageHandling(cc, s_slot);
    }
    '''

GUI_TUNING = r'''else if (pg == 3)                                       // TUNING / ASMA (MM_V7)
    {
        mm3::Ctx cc{ cx0, y0, cw, dt, inputPage, noVeh, pVeh, gtaVeh, P };
        mm3::PageSuspension(cc);
    }
    else if (pg == 6)                                       // TUNING / KOMPONENT (nitro, hidravlika, disk, reng)
    {
        mm3::Ctx cc{ cx0, y0, cw, dt, inputPage, noVeh, pVeh, gtaVeh, P };
        mm3::PageComponents(cc);
    }
    else if (pg == 7)                                       // TUNING / DETALLAR (sokme)
    {
        mm3::Ctx cc{ cx0, y0, cw, dt, inputPage, noVeh, pVeh, gtaVeh, P };
        mm3::PageDetach(cc);
    }
    '''

VEH_ADDCOMP = r'''void CVehicle::AddComponent(int iComponentID)
{
    if (!m_pVehicle || !GamePool_Vehicle_GetAt(m_dwGTAId)) return;
    if (GetVehicleSubtype() != VEHICLE_SUBTYPE_CAR) return;
    if (iComponentID < 1000 || iComponentID > 1193) return;

    // MM_V7: evvel burada exception atilirdi (crash) ve nitro/hidravlika ucun artıq is_component_available yoxlanirdi
    if (!CStreaming::TryLoadModel(iComponentID)) return;

    const bool special = (iComponentID >= 1008 && iComponentID <= 1010) || iComponentID == 1086 || iComponentID == 1087;
    if (!special && !ScriptCommand(&is_component_available, iComponentID)) return;

    uint32_t dwRet = 0;
    ScriptCommand(&add_car_component, m_dwGTAId, iComponentID, &dwRet);

    // skript komandasi isleməyibse birbasa native
    if (!m_pVehicle->HasUpgrade(iComponentID)) m_pVehicle->AddVehicleUpgrade(iComponentID);
    if (!m_pVehicle->HasUpgrade(iComponentID)) return;

    if (iComponentID == 1008) m_pVehicle->m_nNitroBoosts = 5;
    else if (iComponentID == 1009) m_pVehicle->m_nNitroBoosts = 2;
    else if (iComponentID == 1010) m_pVehicle->m_nNitroBoosts = 10;
    else if (iComponentID == 1087) ScriptCommand(&set_car_hydraulics, m_dwGTAId, 1);   // yaradilarkən 0 verilirdi
}

'''

VEH_REMOVE = r'''ScriptCommand(&remove_component, m_dwGTAId, iComponentID);
    if (m_pVehicle->HasUpgrade(iComponentID)) m_pVehicle->RemoveVehicleUpgrade(iComponentID);   // MM_V7 native ehtiyat
    if (iComponentID == 1087) ScriptCommand(&set_car_hydraulics, m_dwGTAId, 0);'''

VEH_PHYS = r'''static std::map<CVehicleGTA*, std::vector<uint8_t>> g_HndxOrig;

// MM_V7: CPhysical kutlesi handling-den AYRI saxlanir -> evvel "Cəki" deyisende real masin deyismirdi
struct HndxPhys { float mass; float turn; };
static std::map<CVehicleGTA*, HndxPhys> g_HndxPhys;

// maxspeed: yalniz bir sahe deyismek kifayet etmir, vites cedveli ve maxVelocity de miqyaslanmalidir
static void HndxScaleSpeed(uintptr_t p, float ratio)
{
    if (!(ratio > 0.01f && ratio < 100.0f)) return;
    tHandlingData* h = (tHandlingData*)p;
    cTransmission& t = h->m_transmissionData;
    t.m_fMaxVelocity *= ratio;
    float* gr = (float*)t.m_aGears;
    const size_t n = sizeof(t.m_aGears) / sizeof(float);
    for (size_t i = 0; i < n; ++i) gr[i] *= ratio;
}'''

VEH_SNAP = r'''g_HndxOrig[g].assign((uint8_t*)p, (uint8_t*)p + HND_SIZE);
    g_HndxPhys[g] = HndxPhys{ g->m_fMass, g->m_fTurnMass };'''

VEH_SET = r'''const float oldVal = *(float*)(p + f->off);
    *(float*)(p + f->off) = val;
    CVehicleGTA* gv = v->m_pVehicle;
    if (f->off == offsetof(tHandlingData, m_fMass)) {
        HndxFixMass(p);
        if (oldVal > 1.0e-4f) gv->m_fMass *= (val / oldVal);          // MM_V7
    }
    if (f->off == offsetof(tHandlingData, m_fTurnMass) && oldVal > 1.0e-4f) gv->m_fTurnMass *= (val / oldVal);
    if (f->off == HND_MAX_VELOCITY && oldVal > 1.0e-4f && val > 0.0f) HndxScaleSpeed(p, val / oldVal);
    if (f->off == offsetof(tHandlingData, m_fSuspensionUpperLimit) ||
        f->off == offsetof(tHandlingData, m_fSuspensionLowerLimit) ||
        f->off == offsetof(tHandlingData, m_fSuspensionBiasBetweenFrontAndRear))
        gv->RefreshSuspensionLines();
    return true;'''

VEH_RESET = r'''memcpy((void*)(p + 4), it->second.data() + 4, HND_SIZE - 4);
    {   // MM_V7: CPhysical kutlesini de qaytar + asma xetlerini yeniden qur
        std::map<CVehicleGTA*, HndxPhys>::iterator ph = g_HndxPhys.find(v->m_pVehicle);
        if (ph != g_HndxPhys.end()) { v->m_pVehicle->m_fMass = ph->second.mass; v->m_pVehicle->m_fTurnMass = ph->second.turn; }
        v->m_pVehicle->RefreshSuspensionLines();
    }'''

H_DECL = r'''void RemoveVehicleUpgrade(int32 upgradeModelIndex);
    // MM_V7
    bool HasUpgrade(int32 modelId) const { for (int i = 0; i < 15; ++i) if (m_anUpgrades[i] == modelId) return true; return false; }
    void RefreshSuspensionLines();'''

CPP_DEF = r'''CHook::CallFunction<void>(g_libGTASA + (VER_x32 ? 0x58CC2C + 1 : 0x6B0718), this, upgradeModelIndex);
}

// MM_V7: asma (susp) handling deyisenden sonra xetlerin yeniden hesablanmasi (yoxsa hundurluk deyismir)
void CVehicleGTA::RefreshSuspensionLines() {
    if (m_nVehicleSubType != VEHICLE_TYPE_AUTOMOBILE) return;
    CHook::CallFunction<void>("_ZN11CAutomobile20SetupSuspensionLinesEv", this);
}'''

MM3 = r'''// ===== MM_V7: sade handling, tuning alt bolmeleri (Asma / Komponent / Detallar) =====
#include "../game/VehDetach.h"

namespace mm3
{
    struct Ctx {
        float cx0, y0, cw, dt;
        bool  in, noVeh;
        CVehicle*    veh;
        CVehicleGTA* g;
        mm::Rc       P;
    };

    // basili saxlayanda tekrarlanan duyme: basanda 1, sonra surətlənir
    static int Repeat(int id, const mm::Rc& r, float dt, bool can)
    {
        using namespace mm;
        static float s_hold = 0.0f, s_acc = 0.0f;
        if (!can) return 0;
        const MMTouch& t = g_mmTouch;
        int fire = 0;
        if (t.pressed && s_act == 0 && In(r, t.x, t.y)) {
            s_act = id; s_hold = 0.0f; s_acc = 0.0f; fire = 1;
        } else if (s_act == id && t.down && In(r, t.x, t.y)) {
            s_hold += dt;
            if (s_hold > 0.4f) {
                s_acc += dt;
                const float per = (s_hold > 1.6f) ? 0.03f : 0.09f;
                while (s_acc >= per && fire < 8) { s_acc -= per; ++fire; }
            }
        }
        return fire;
    }

    static void Glyph(const mm::Rc& r, bool plus, bool hot)
    {
        using namespace mm;
        Fill(r, Col(255, 255, 255, hot ? 70 : 30), S(8.0f));
        const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f, k = S(6.0f);
        s_dl->AddLine(ImVec2(cx - k, cy), ImVec2(cx + k, cy), Col(240, 240, 240, 240), S(2.0f));
        if (plus) s_dl->AddLine(ImVec2(cx, cy - k), ImVec2(cx, cy + k), Col(240, 240, 240, 240), S(2.0f));
    }

    // [etiket ...... [-] [deyer] [+]]  ->  -n / +n (basma sayi); valTap = deyere toxunuldu
    static int AdjRow(int id, const mm::Rc& row, const char* label, const char* val, float dt, bool can, bool* valTap = nullptr)
    {
        using namespace mm;
        Fill(row, Col(255, 255, 255, 14), S(9.0f));
        Txt(S(15.0f), row.x + S(12.0f), row.y + (row.h - S(15.0f)) * 0.5f, Col(228, 228, 228), label);
        const float bh = row.h - S(6.0f), bw = S(34.0f), y = row.y + S(3.0f);
        const Rc pl{ row.x + row.w - bw - S(3.0f), y, bw, bh };
        const Rc vr{ pl.x - S(6.0f) - S(96.0f), y, S(96.0f), bh };
        const Rc mn{ vr.x - S(6.0f) - bw, y, bw, bh };
        int n = 0;
        n -= Repeat(id, mn, dt, can);
        n += Repeat(id + 1, pl, dt, can);
        if (valTap && Tap(id + 2, vr, can)) *valTap = true;
        Glyph(mn, false, s_act == id);
        Glyph(pl, true,  s_act == id + 1);
        Fill(vr, Col(0, 0, 0, 100), S(7.0f));
        TxtC(S(15.0f), vr, Col(255, 255, 255), val, true);
        return n;
    }

    // acar-setir (sade, ag/qara/boz)
    static bool Chip(int id, const mm::Rc& r, const char* label, const char* sub, bool on, bool enabled, float dt, bool can, int anim)
    {
        using namespace mm;
        static float s_a[32] = {};
        const bool hit = enabled && Tap(id, r, can);
        float& a = s_a[anim & 31];
        a = Smooth(a, on ? 1.0f : 0.0f, dt, 16.0f);
        Fill(r, Col(255, 255, 255, enabled ? (s_act == id ? 26 : 14) : 6), S(9.0f));
        Txt(S(16.0f), r.x + S(12.0f), r.y + S(5.0f), enabled ? Col(238, 238, 238) : Col(110, 110, 110), label);
        Txt(S(12.0f), r.x + S(12.0f), r.y + r.h - S(19.0f), Col(132, 132, 132), sub);
        if (enabled) {
            const Rc tr{ r.x + r.w - S(54.0f), r.y + (r.h - S(24.0f)) * 0.5f, S(42.0f), S(24.0f) };
            Fill(tr, Col((int)Lerp(70.0f, 240.0f, a), (int)Lerp(70.0f, 240.0f, a), (int)Lerp(70.0f, 240.0f, a)), S(12.0f));
            Dot(tr.x + S(12.0f) + a * (tr.w - S(24.0f)), tr.y + tr.h * 0.5f, S(9.0f), on ? Col(20, 20, 20) : Col(200, 200, 200));
        }
        return hit;
    }

    static void NoVeh(const Ctx& c, const char* sub)
    {
        mm2::EmptyState(mm::Rc{ c.cx0, c.y0, c.cw, mm::S(300.0f) }, "Avtomobildə deyilsiniz", sub);
    }

    // ------------------------------------------------------------------ HANDLING
    static int HCat(const char* k)
    {
        if (!strncmp(k, "susp", 4)) return 2;
        static const char* const drv[] = { "accel", "maxspeed", "brake", "brakebias", "steering", "traction", "tractionloss", "tractionbias" };
        for (const char* d : drv) if (!strcmp(k, d)) return 1;
        return 0;
    }

    static float HStep(const char* k, float cur)
    {
        if (!strncmp(k, "com", 3)) return 0.05f;
        const float a = fabsf(cur);
        return a > 1.0e-4f ? a * 0.05f : 0.01f;
    }

    static void PageHandling(const Ctx& c, int& slot)
    {
        using namespace mm;
        static int  s_cat = 0, s_edit = -1;
        static char s_buf[20] = "";
        if (c.noVeh || !c.veh) {
            s_edit = -1;
            NoVeh(c, "Handling yalnız maşının içində dəyişdirilir");
            return;
        }
        CVehicle* pVeh = c.veh;
        const float dt = c.dt;
        const bool lin = c.in && s_edit < 0;      // rəqəm paneli açıqkən siyahı toxunmur

        // 1) slotlar + fayl düymələri
        for (int i = 0; i < 5; ++i) {
            const Rc b{ c.cx0 + S(40.0f) * i, c.y0, S(36.0f), S(34.0f) };
            if (Tap(80 + i, b, lin)) slot = i + 1;
            const bool on = (slot == i + 1);
            Fill(b, on ? Col(238, 238, 238) : Col(255, 255, 255, 18), S(9.0f));
            char nb[4];
            snprintf(nb, sizeof(nb), "%d", i + 1);
            TxtC(S(16.0f), b, on ? Col(12, 12, 12) : Col(200, 200, 200), nb, true);
            if (HndExt_SlotUsed(i + 1)) Dot(b.x + b.w - S(7.0f), b.y + S(7.0f), S(3.0f), Col(80, 220, 140));
        }
        static const char* const kAct[3] = { "Fayl seç", "Tətbiq et", "Sıfırla" };
        {
            const float bx = c.cx0 + S(208.0f), gap = S(8.0f);
            const float bw = (c.cw - S(208.0f) - gap * 2.0f) / 3.0f;
            for (int k = 0; k < 3; ++k) {
                const Rc b{ bx + k * (bw + gap), c.y0, bw, S(34.0f) };
                if (!mm2::SmallBtn(85 + k, b, kAct[k], dt, lin, k == 1)) continue;
                if (k == 0) {
                    if (pJavaWrapper) {
                        const std::string dst = HndExt_SlotPath(slot);
                        pJavaWrapper->PickHandlingFile(slot, dst.c_str());
                        mm2::SetMsg("handling.cfg faylını seçin → slot " + std::to_string(slot));
                    }
                } else if (k == 1) {
                    std::string m;
                    HndExt_ApplyFileSlot(pVeh, slot, m);
                    mm2::SetMsg(m);
                } else {
                    mm2::SetMsg(HndExt_Reset(pVeh) ? "Standarta qaytarıldı" : "Dəyişiklik yoxdur");
                }
            }
        }

        // 2) kateqoriyalar
        static const char* const kCat[3] = { "Əsas", "Sürüş", "Asma" };
        for (int i = 0; i < 3; ++i) {
            const Rc b{ c.cx0 + S(112.0f) * i, c.y0 + S(42.0f), S(106.0f), S(28.0f) };
            if (Tap(310 + i, b, lin)) s_cat = i;
            const bool on = (s_cat == i);
            Fill(b, on ? Col(238, 238, 238) : Col(255, 255, 255, 16), S(9.0f));
            TxtC(S(14.0f), b, on ? Col(12, 12, 12) : Col(190, 190, 190), kCat[i], on);
        }
        if (mm2::s_msgT > 0.01f)
            Txt(S(13.0f), c.cx0 + S(350.0f), c.y0 + S(48.0f), Col(120, 215, 160, (int)(255.0f * mm2::s_msgT)), mm2::s_msg);
        else
            Txt(S(13.0f), c.cx0 + S(350.0f), c.y0 + S(48.0f), Col(130, 130, 130), "- / + = dəyiş (basılı saxla), dəyərə toxun = rəqəm yaz");

        // 3) sətirlər
        const float ry0 = c.y0 + S(78.0f), rh = S(36.0f), rg = S(2.0f);
        int shown = 0;
        for (int r = 0; r < mm2::kHFN; ++r) {
            const char* key = mm2::kHF[r].key;
            if (HCat(key) != s_cat) continue;
            const Rc row{ c.cx0, ry0 + (rh + rg) * (float)shown, c.cw, rh };
            ++shown;
            float cur = 0.0f;
            const bool have = HndExt_Get(pVeh, key, &cur);
            char vb[32] = "-";
            if (have) snprintf(vb, sizeof(vb), mm2::kHF[r].fmt, cur);
            bool vt = false;
            const int n = AdjRow(300 + r * 3, row, mm2::kHF[r].label, vb, dt, lin && have, &vt);
            if (have && n != 0) {
                float v = cur;
                const int cnt = n < 0 ? -n : n;
                for (int k = 0; k < cnt; ++k) v += (n < 0 ? -1.0f : 1.0f) * HStep(key, v);
                if (strncmp(key, "com", 3) != 0 && v < 0.0f) v = 0.0f;
                if (!strcmp(key, "mass") && v < 1.0f) v = 1.0f;
                HndExt_Set(pVeh, key, v);
            }
            if (have && vt) {
                s_edit = r;
                snprintf(s_buf, sizeof(s_buf), "%.5g", cur);
            }
        }

        // 4) menyu daxili rəqəm paneli (sistem klaviaturası / chat YOXDUR)
        if (s_edit >= 0) {
            Fill(c.P, Col(0, 0, 0, 150), S(28.0f));
            const Rc np{ c.P.x + (c.P.w - S(300.0f)) * 0.5f, c.P.y + S(112.0f), S(300.0f), S(344.0f) };
            Fill(np, Col(24, 24, 24, 252), S(16.0f));
            Stroke(np, Col(255, 255, 255, 60), S(16.0f), S(1.0f));
            TxtB(S(16.0f), np.x + S(16.0f), np.y + S(12.0f), Col(240, 240, 240), mm2::kHF[s_edit].label);
            const Rc fld{ np.x + S(16.0f), np.y + S(40.0f), np.w - S(32.0f), S(46.0f) };
            Fill(fld, Col(0, 0, 0, 140), S(10.0f));
            std::string shown2 = s_buf;
            shown2 += "|";
            TxtC(S(22.0f), fld, Col(255, 255, 255), shown2.c_str(), true);

            static const char* const kKeys[15] = { "7", "8", "9", "4", "5", "6", "1", "2", "3", ".", "0", "-", "Sil", "Ləğv", "OK" };
            const float kw = (np.w - S(32.0f) - S(12.0f)) / 3.0f, kh = S(40.0f);
            for (int i = 0; i < 15; ++i) {
                const Rc kr{ np.x + S(16.0f) + (float)(i % 3) * (kw + S(6.0f)), np.y + S(98.0f) + (float)(i / 3) * (kh + S(6.0f)), kw, kh };
                if (!mm2::SmallBtn(400 + i, kr, kKeys[i], dt, c.in, i == 14)) continue;
                const char* k = kKeys[i];
                const size_t len = strlen(s_buf);
                if (i < 12) {
                    if (k[0] == '.') {
                        if (!strchr(s_buf, '.') && len < 14) {
                            if (len == 0 || (len == 1 && s_buf[0] == '-')) strcat(s_buf, "0");
                            strcat(s_buf, ".");
                        }
                    } else if (k[0] == '-') {
                        if (s_buf[0] == '-') memmove(s_buf, s_buf + 1, len);
                        else if (len < 14) { memmove(s_buf + 1, s_buf, len + 1); s_buf[0] = '-'; }
                    } else if (len < 14) {
                        s_buf[len] = k[0];
                        s_buf[len + 1] = 0;
                    }
                } else if (i == 12) {
                    if (len) s_buf[len - 1] = 0;
                } else if (i == 13) {
                    s_edit = -1;
                } else {
                    char* end = nullptr;
                    const float f = strtof(s_buf, &end);
                    if (s_buf[0] == 0 || end == s_buf) mm2::SetMsg("Yanlış dəyər");
                    else if (HndExt_Set(pVeh, mm2::kHF[s_edit].key, f)) mm2::SetMsg(std::string(mm2::kHF[s_edit].label) + " = " + s_buf);
                    else mm2::SetMsg("Dəyər qəbul edilmədi");
                    s_edit = -1;
                }
                break;
            }
        }
    }

    // ------------------------------------------------------------------ TUNING / ASMA
    static void PageSuspension(const Ctx& c)
    {
        using namespace mm;
        if (c.noVeh || !c.veh || !c.g) { NoVeh(c, "Tuning yalnız maşının içində işləyir"); return; }
        CVehicle* v = c.veh;
        CVehicleGTA* g = c.g;
        const float dt = c.dt;

        static CVehicleGTA* s_bv = nullptr;
        static float s_baseLow = 0.0f;
        if (g != s_bv) {
            s_bv = g;
            float lo = 0.0f;
            s_baseLow = HndExt_Get(v, "susplower", &lo) ? lo : 0.0f;
        }

        const float colW = (c.cw - S(16.0f)) * 0.5f, rh = S(40.0f), rg = S(8.0f);
        auto row = [&](int i) { return Rc{ c.cx0, c.y0 + (rh + rg) * (float)i, colW, rh }; };
        char vb[32];

        {   // hündürlük (yuxarı + aşağı hədd birlikdə)
            float lo = 0.0f, up = 0.0f;
            const bool ok = HndExt_Get(v, "susplower", &lo) && HndExt_Get(v, "suspupper", &up);
            snprintf(vb, sizeof(vb), ok ? "%+.2f" : "-", lo - s_baseLow);
            const int n = AdjRow(340, row(0), "Hündürlük", vb, dt, c.in && ok);
            if (ok && n != 0) {
                const float st = 0.02f * (float)n;
                if (fabsf((lo - s_baseLow) + st) <= 0.4f) {
                    HndExt_Set(v, "susplower", lo + st);
                    HndExt_Set(v, "suspupper", up + st);
                }
            }
        }
        {   // ön / arxa balans
            float b = 0.5f;
            const bool ok = HndExt_Get(v, "suspbias", &b);
            snprintf(vb, sizeof(vb), ok ? "%.2f" : "-", b);
            const int n = AdjRow(343, row(1), "Ön / arxa balans", vb, dt, c.in && ok);
            if (ok && n != 0) HndExt_Set(v, "suspbias", Clamp(b + 0.05f * (float)n, 0.05f, 0.95f));
        }
        {   // sərtlik
            float f = 0.0f;
            const bool ok = HndExt_Get(v, "suspforce", &f);
            snprintf(vb, sizeof(vb), ok ? "%.2f" : "-", f);
            const int n = AdjRow(346, row(2), "Sərtlik", vb, dt, c.in && ok && f > 1.0e-4f);
            if (ok && n != 0) HndExt_Set(v, "suspforce", f * powf(1.1f, (float)n));
        }
        {   // amortizator
            float f = 0.0f;
            const bool ok = HndExt_Get(v, "suspdamp", &f);
            snprintf(vb, sizeof(vb), ok ? "%.2f" : "-", f);
            const int n = AdjRow(349, row(3), "Amortizator", vb, dt, c.in && ok && f > 1.0e-4f);
            if (ok && n != 0) HndExt_Set(v, "suspdamp", f * powf(1.1f, (float)n));
        }
        {   // təkər ölçüsü
            float ws = g->m_wheelScale;
            if (ws < 0.05f || ws > 5.0f) ws = 1.0f;
            snprintf(vb, sizeof(vb), "%.2f", ws);
            const int n = AdjRow(352, row(4), "Təkər ölçüsü", vb, dt, c.in);
            if (n != 0) g->m_wheelScale = Clamp(ws + 0.05f * (float)n, 0.5f, 2.0f);
        }

        const float rx = c.cx0 + colW + S(16.0f);
        Txt(S(14.0f), rx, c.y0 + S(4.0f), Col(200, 200, 200), "Kamera maşının ətrafında dönür");
        if (mm2::SmallBtn(380, Rc{ rx, c.y0 + S(30.0f), colW, S(48.0f) }, "Qaraj kamerası", dt, c.in, true)) Garage::Begin(g);
        if (mm2::SmallBtn(381, Rc{ rx, c.y0 + S(90.0f), colW, S(40.0f) }, "Hamısını standarta qaytar", dt, c.in, false)) {
            HndExt_Reset(v);
            g->m_wheelScale = 1.0f;
            s_bv = nullptr;
            mm2::SetMsg("Standarta qaytarıldı");
        }
        if (mm2::s_msgT > 0.01f)
            Txt(S(13.0f), rx, c.y0 + S(142.0f), Col(120, 215, 160, (int)(255.0f * mm2::s_msgT)), mm2::s_msg);
    }

    // ------------------------------------------------------------------ TUNING / KOMPONENT
    struct QDef { int id; const char* name; };

    static void PageComponents(const Ctx& c)
    {
        using namespace mm;
        if (c.noVeh || !c.veh || !c.g) { NoVeh(c, "Tuning yalnız maşının içində işləyir"); return; }
        CVehicle* v = c.veh;
        CVehicleGTA* g = c.g;
        const float dt = c.dt;

        static const QDef kQ[5] = { { 1009, "Nitro x2" }, { 1008, "Nitro x5" }, { 1010, "Nitro x10" }, { 1087, "Hidravlika" }, { 1086, "Stereo" } };
        static const int kWid[17] = { 1025, 1073, 1074, 1075, 1076, 1077, 1078, 1079, 1080, 1081, 1082, 1083, 1084, 1085, 1096, 1097, 1098 };
        static const char* const kWn[17] = { "Offroad", "Shadow", "Mega", "Rimshine", "Wires", "Classic", "Twist", "Cutter", "Switch", "Grove", "Import", "Dollar", "Trance", "Atomic", "Ahab", "Virtual", "Access" };

        const float colW = (c.cw - S(16.0f)) * 0.5f;
        Txt(S(15.0f), c.cx0, c.y0 + S(2.0f), Col(190, 190, 190), "Komponentlər");
        for (int i = 0; i < 5; ++i) {
            const Rc r{ c.cx0, c.y0 + S(26.0f) + (S(46.0f) + S(4.0f)) * (float)i, colW, S(46.0f) };
            const int id = kQ[i].id;
            const bool has = g->HasUpgrade(id);
            char sub[24];
            snprintf(sub, sizeof(sub), "ID %d", id);
            if (!Chip(390 + i, r, kQ[i].name, has ? "Qoşulub" : sub, has, true, dt, c.in, 16 + i)) continue;
            try {
                if (has) {
                    v->RemoveComponent(id);
                    if (id >= 1008 && id <= 1010) {
                        bool any = false;
                        for (int o = 1008; o <= 1010; ++o) any = any || g->HasUpgrade(o);
                        if (!any) g->m_nNitroBoosts = 0;
                    }
                    mm2::SetMsg(std::string(kQ[i].name) + " sökülüb");
                } else {
                    if (id >= 1008 && id <= 1010)
                        for (int o = 1008; o <= 1010; ++o) if (o != id && g->HasUpgrade(o)) v->RemoveComponent(o);
                    v->AddComponent(id);
                    mm2::SetMsg(g->HasUpgrade(id) ? std::string(kQ[i].name) + " qoşuldu" : std::string("Qoşulmadı (model uyğun deyil)"));
                }
            } catch (...) {
                mm2::SetMsg("Model yüklənmədi");
            }
        }
        {
            char nb[40];
            snprintf(nb, sizeof(nb), "Nitro sayı: %d", (int)g->m_nNitroBoosts);
            Txt(S(13.0f), c.cx0 + S(4.0f), c.y0 + S(286.0f), Col(150, 150, 150), nb);
        }
        if (mm2::s_msgT > 0.01f)
            Txt(S(13.0f), c.cx0 + S(4.0f), c.y0 + S(308.0f), Col(120, 215, 160, (int)(255.0f * mm2::s_msgT)), mm2::s_msg);

        // sağ sütun
        const float rx = c.cx0 + colW + S(16.0f);
        static CVehicleGTA* s_cv = nullptr;
        static int s_w = -1, s_id = 1000;
        if (g != s_cv) {
            s_cv = g;
            s_w = -1;
            for (int i = 0; i < 17; ++i) if (g->HasUpgrade(kWid[i])) s_w = i;
        }
        auto rrow = [&](int i) { return Rc{ rx, c.y0 + (S(36.0f) + S(6.0f)) * (float)i, colW, S(36.0f) }; };

        {   // disk
            const int n = AdjRow(350, rrow(0), "Disk", s_w >= 0 ? kWn[s_w] : "Standart", dt, c.in);
            if (n != 0) {
                int nw = s_w + (n > 0 ? 1 : -1);
                if (nw < 0) nw = 0;
                if (nw > 16) nw = 16;
                if (nw != s_w) {
                    s_w = nw;
                    try { v->AddComponent(kWid[nw]); } catch (...) { mm2::SetMsg("Model yüklənmədi"); }
                }
            }
        }
        {   // komponent ID
            char b[12];
            snprintf(b, sizeof(b), "%d", s_id);
            const int n = AdjRow(353, rrow(1), "Komponent ID", b, dt, c.in);
            if (n != 0) s_id = (int)Clamp((float)(s_id + n), 1000.0f, 1193.0f);
        }
        {
            const Rc b1{ rx, rrow(2).y, (colW - S(8.0f)) * 0.5f, S(36.0f) };
            const Rc b2{ b1.x + b1.w + S(8.0f), b1.y, b1.w, b1.h };
            if (mm2::SmallBtn(382, b1, "Qoş", dt, c.in, true)) {
                try {
                    v->AddComponent(s_id);
                    mm2::SetMsg(g->HasUpgrade(s_id) ? "Qoşuldu" : "Qoşulmadı (model uyğun deyil)");
                } catch (...) { mm2::SetMsg("Model yüklənmədi"); }
            }
            if (mm2::SmallBtn(383, b2, "Sök", dt, c.in, false)) {
                v->RemoveComponent(s_id);
                mm2::SetMsg("Sökülüb");
            }
        }
        {   // rəng
            int c1 = g->m_nPrimaryColor, c2 = g->m_nSecondaryColor;
            char b[12];
            snprintf(b, sizeof(b), "%d", c1);
            int n = AdjRow(356, rrow(3), "Rəng 1", b, dt, c.in);
            if (n != 0) v->SetColor((uint8_t)Clamp((float)(c1 + n), 0.0f, 126.0f), (uint8_t)c2);
            snprintf(b, sizeof(b), "%d", c2);
            n = AdjRow(359, rrow(4), "Rəng 2", b, dt, c.in);
            if (n != 0) v->SetColor((uint8_t)c1, (uint8_t)Clamp((float)(c2 + n), 0.0f, 126.0f));
        }
        if (mm2::SmallBtn(384, rrow(5), "Bütün komponentləri sök", dt, c.in, false)) {
            for (int i = 0; i < 15; ++i) {
                const int id = g->m_anUpgrades[i];
                if (id > 0) v->RemoveComponent(id);
            }
            g->m_nNitroBoosts = 0;
            s_w = -1;
            mm2::SetMsg("Hamısı sökülüb");
        }
    }

    // ------------------------------------------------------------------ TUNING / DETALLAR
    static void PageDetach(const Ctx& c)
    {
        using namespace mm;
        if (c.noVeh || !c.veh || !c.g) { NoVeh(c, "Detallar yalnız maşının içində sökülür"); return; }
        CVehicleGTA* g = c.g;
        const float dt = c.dt;

        static CVehicleGTA* s_av = nullptr;
        static uint32_t s_ct = 0, s_mask = 0;
        if (g != s_av || (uint32_t)g->m_nCreationTime != s_ct) {
            s_av = g;
            s_ct = (uint32_t)g->m_nCreationTime;
            s_mask = 0;
            for (int p = 0; p < VehDetach::P_COUNT; ++p) if (VehDetach::Available(g, p)) s_mask |= (1u << p);
        }

        const float bw = (c.cw - S(8.0f)) * 0.5f;
        if (mm2::SmallBtn(420, Rc{ c.cx0, c.y0, bw, S(34.0f) }, "Hamısını sök", dt, c.in, false)) VehDetach::HideAll(g);
        if (mm2::SmallBtn(421, Rc{ c.cx0 + bw + S(8.0f), c.y0, bw, S(34.0f) }, "Hamısını qaytar", dt, c.in, true)) VehDetach::ShowAll(g);

        const float rh = S(40.0f), rg = S(4.0f);
        for (int p = 0; p < VehDetach::P_COUNT; ++p) {
            const int col = p / 8, ri = p % 8;
            const Rc r{ c.cx0 + (float)col * (bw + S(8.0f)), c.y0 + S(44.0f) + (rh + rg) * (float)ri, bw, rh };
            const bool avail = ((s_mask >> p) & 1u) != 0;
            const bool hid = VehDetach::IsHidden(g, p);
            if (Chip(430 + p, r, VehDetach::kLabel[p], !avail ? "Bu modeldə yoxdur" : (hid ? "Sökülüb" : "Yerindədir"), hid, avail, dt, c.in, p))
                VehDetach::Toggle(g, p);
        }
    }
}

'''

VEHDETACH_H = r'''#pragma once
// Masin detallarinin sokulmesi (yalniz YERLI ekranda gorunur).
struct CVehicleGTA;

namespace VehDetach
{
    enum Part {
        P_HOOD, P_TRUNK, P_BUMP_F, P_BUMP_R,
        P_DOOR_LF, P_DOOR_RF, P_DOOR_LR, P_DOOR_RR,
        P_WING_LF, P_WING_RF, P_HEADLIGHTS, P_TAILLIGHTS,
        P_WINDSCREEN, P_EXHAUST, P_EXTRAS, P_COUNT
    };

    extern const char* const kLabel[P_COUNT];

    bool Available(CVehicleGTA* v, int part);   // bu modelde bele hisse varmi
    bool IsHidden (CVehicleGTA* v, int part);
    bool Toggle   (CVehicleGTA* v, int part);   // true = deyisdi
    void ShowAll  (CVehicleGTA* v);
    void HideAll  (CVehicleGTA* v);

    // CVehicleModelInfo::SetupLightFlags hook-undan (render zamani, her kadr) cagirilir
    void Apply(CVehicleGTA* v);
}
'''

VEHDETACH_CPP = r'''#include "VehDetach.h"
#include <unordered_map>
#include <vector>
#include <cstring>
#include "game/Entity/CVehicleGTA.h"
#include "game/Models/ClumpModelInfo.h"
#include "game/RW/rwcore.h"
#include "game/RW/rpworld.h"

namespace VehDetach
{
    const char* const kLabel[P_COUNT] = {
        "Kapot", "Baqaj", "Ön bufer", "Arxa bufer",
        "Sol ön qapı", "Sağ ön qapı", "Sol arxa qapı", "Sağ arxa qapı",
        "Sol ön qanad", "Sağ ön qanad", "Fara", "Stop",
        "Ön şüşə", "Ekzoz", "Əlavə detallar"
    };

    // GTA SA vehicle.dff frame adlari (modele gore bezileri olmaya biler)
    static const char* const kFrames[P_COUNT][6] = {
        { "bonnet_dummy" },
        { "boot_dummy" },
        { "bump_front_dummy" },
        { "bump_rear_dummy" },
        { "door_lf_dummy" },
        { "door_rf_dummy" },
        { "door_lr_dummy" },
        { "door_rr_dummy" },
        { "wing_lf_dummy" },
        { "wing_rf_dummy" },
        { "headlights", "headlights2" },
        { "taillights", "taillights2" },
        { "windscreen_dummy" },
        { "exhaust_ok" },
        { "extra1", "extra2", "extra3", "extra4", "extra5", "extra6" },
    };

    constexpr uint8_t kRenderFlag = 0x04;     // rpATOMICRENDER
    constexpr uint8_t kTypeAtomic = 1;        // rpATOMIC

    struct Saved { RwObject* o; uint8_t flags; };

    struct State {
        uint32_t              ctime = 0;
        void*                 clump = nullptr;
        uint32_t              mask  = 0;
        std::vector<Saved>    saved[P_COUNT];
    };
    static std::unordered_map<CVehicleGTA*, State> g_st;

    static RwObject* CollectCB(RwObject* o, void* d)
    {
        if (o && o->type == kTypeAtomic)
            static_cast<std::vector<Saved>*>(d)->push_back({ o, (uint8_t)o->flags });
        return o;
    }

    static void Walk(RwFrame* f, std::vector<Saved>& out)
    {
        if (!f) return;
        RwFrameForAllObjects(f, CollectCB, &out);
        for (RwFrame* c = f->child; c; c = c->next) Walk(c, out);
    }

    static void Collect(CVehicleGTA* v, int part, std::vector<Saved>& out)
    {
        for (const char* nm : kFrames[part]) {
            if (!nm) break;
            RwFrame* fr = CClumpModelInfo::GetFrameFromName((RpClump*)v->m_pRwClump, nm);
            if (fr) Walk(fr, out);
        }
    }

    static State* Get(CVehicleGTA* v, bool create)
    {
        if (!v || !v->m_pRwClump) return nullptr;
        auto it = g_st.find(v);
        if (it != g_st.end()) {
            State& s = it->second;
            // basqa masin eyni adresde / klump yeniden yuklenib -> kohne pointerlere TOXUNMADAN sifirla
            if (s.ctime != v->m_nCreationTime || s.clump != (void*)v->m_pRwClump) g_st.erase(it);
            else return &s;
        }
        if (!create) return nullptr;
        State& s = g_st[v];
        s.ctime = v->m_nCreationTime;
        s.clump = (void*)v->m_pRwClump;
        return &s;
    }

    bool Available(CVehicleGTA* v, int part)
    {
        if (!v || !v->m_pRwClump || part < 0 || part >= P_COUNT) return false;
        State* s = Get(v, false);
        if (s && (s->mask & (1u << part))) return true;     // artiq sokulub
        std::vector<Saved> tmp;
        Collect(v, part, tmp);
        return !tmp.empty();
    }

    bool IsHidden(CVehicleGTA* v, int part)
    {
        State* s = Get(v, false);
        return s && (s->mask & (1u << part));
    }

    bool Toggle(CVehicleGTA* v, int part)
    {
        if (part < 0 || part >= P_COUNT) return false;
        State* s = Get(v, true);
        if (!s) return false;
        const uint32_t bit = 1u << part;
        if (s->mask & bit) {                                  // geri qoy
            for (auto& sv : s->saved[part]) sv.o->flags = sv.flags;
            s->saved[part].clear();
            s->mask &= ~bit;
            return true;
        }
        s->saved[part].clear();
        Collect(v, part, s->saved[part]);
        if (s->saved[part].empty()) return false;
        for (auto& sv : s->saved[part]) sv.o->flags &= ~kRenderFlag;
        s->mask |= bit;
        return true;
    }

    void ShowAll(CVehicleGTA* v)
    {
        for (int p = 0; p < P_COUNT; ++p) if (IsHidden(v, p)) Toggle(v, p);
    }

    void HideAll(CVehicleGTA* v)
    {
        for (int p = 0; p < P_COUNT; ++p) if (!IsHidden(v, p)) Toggle(v, p);
    }

    void Apply(CVehicleGTA* v)
    {
        if (g_st.empty()) return;
        State* s = Get(v, false);
        if (!s || !s->mask) return;
        for (int p = 0; p < P_COUNT; ++p)
            if (s->mask & (1u << p))
                for (auto& sv : s->saved[p]) sv.o->flags &= ~kRenderFlag;   // GTA geri yandirsa yeniden sondur
    }
}
'''


def replace_all_optional(text, anchor, new):
    rx = ws_re(anchor)
    return rx.sub(lambda m: new, text)


def patch_gui(t):
    if TAG in t:
        raise PatchError("gui.cpp artiq patch olunub (%s)" % TAG)
    # tema
    t = replace1(t, r'''static inline ImU32 Col(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, (int)((float)a * s_A)); }
        static inline ImU32 Accent(float /*istifade olunmur*/, int a = 255) { return Col(78, 142, 235, a); }''',
                 GUI_COL, "gui:Col/Accent")
    t = replace1(t, r'''Fill(d, primary ? Accent(0.0f, 200 + (int)(40.0f * p)) : Col(255, 255, 255, 20 + (int)(30.0f * p)), S(12.0f));
        TxtC(S(15.0f), d, Col(244, 247, 253), label, true);''', GUI_SMALLBTN, "gui:SmallBtn")
    t = replace_all_optional(t, "Fill(pvr, Col((int)s_hl[0], (int)s_hl[1], (int)s_hl[2], 255), S(12.0f));",
                             "Fill(pvr, ColT((int)s_hl[0], (int)s_hl[1], (int)s_hl[2], 255), S(12.0f));")
    t = replace_all_optional(t, "Dot(cxs, cys, S(20.0f) - S(2.0f) * p, Col(r, g, b, 255));",
                             "Dot(cxs, cys, S(20.0f) - S(2.0f) * p, ColT(r, g, b, 255));")
    # yeni UI blokunu RenderHandlingDialog-dan evvel qoy
    t = insert_before(t, "void RenderHandlingDialog() {", MM3 + "\n", "gui:mm3")
    # tablar
    t = replace_between(t, "// segmentli idareetme (iOS)", "// ---- sehife mezmunu ----", GUI_TABS, "gui:tabs")
    t = replace1(t, "const float y0 = P.y + S(124.0f);",
                 "const float y0  = P.y + S(124.0f) + (s_tab == 2 ? S(26.0f) : 0.0f);", "gui:y0")
    # handling
    t = replace_between(t, "else if (s_tab == 1) // HANDLING", "else if (s_tab == 2) // FAR", GUI_HANDLING, "gui:handling")
    t = replace1(t, "else if (s_tab == 2) // FAR", "else if (pg == 2)                                       // FAR (Tuning / İşıq)", "gui:far")
    # tuning
    t = replace_between(t, "else if (s_tab == 3) // TUNING (GARAGE_V5)", "else if (s_tab == 4) // NOMRE (PLATEADJ_V7)", GUI_TUNING, "gui:tuning")
    t = replace1(t, "else if (s_tab == 4) // NOMRE (PLATEADJ_V7)", "else if (pg == 4)                                       // NOMRE (Tuning / Nömrə)", "gui:nomre")
    return t + "\n// " + TAG + "\n"


def patch_vehicle_cpp(t):
    if TAG in t:
        raise PatchError("vehicle.cpp artiq patch olunub")
    t = replace_between(t, "void CVehicle::AddComponent(int iComponentID)", "void CVehicle::SetPaintJob(uint8_t bytePaintJobID)", VEH_ADDCOMP, "veh:AddComponent")
    t = replace1(t, "ScriptCommand(&remove_component, m_dwGTAId, iComponentID);", VEH_REMOVE, "veh:RemoveComponent")
    t = replace1(t, "static std::map<CVehicleGTA*, std::vector<uint8_t>> g_HndxOrig;", VEH_PHYS, "veh:phys")
    t = replace1(t, "g_HndxOrig[g].assign((uint8_t*)p, (uint8_t*)p + HND_SIZE);", VEH_SNAP, "veh:snap")
    t = replace1(t, '''*(float*)(p + f->off) = val;
    if (f->off == offsetof(tHandlingData, m_fMass)) HndxFixMass(p);
    return true;''', VEH_SET, "veh:Set")
    t = replace1(t, "memcpy((void*)(p + 4), it->second.data() + 4, HND_SIZE - 4);", VEH_RESET, "veh:Reset")
    return t + "\n// " + TAG + "\n"


def patch_vehgta_h(t):
    if TAG in t:
        raise PatchError("CVehicleGTA.h artiq patch olunub")
    return replace1(t, "void RemoveVehicleUpgrade(int32 upgradeModelIndex);", H_DECL, "vgh")


def patch_vehgta_cpp(t):
    if TAG in t:
        raise PatchError("CVehicleGTA.cpp artiq patch olunub")
    t = replace1(t, "CHook::CallFunction<void>(g_libGTASA + (VER_x32 ? 0x58CC2C + 1 : 0x6B0718), this, upgradeModelIndex); }",
                 CPP_DEF, "vgc:def")
    t = insert_before(t, "void (*CVehicleModelInfo__SetupLightFlags)(CVehicleGTA* veh);",
                      '#include "../VehDetach.h"   // MM_V7\n', "vgc:inc")
    t = replace1(t, "VehControls::ApplyLampArray(veh);",
                 "VehControls::ApplyLampArray(veh);\n    VehDetach::Apply(veh);   // MM_V7", "vgc:hook")
    return t


def patch_cmake():
    """samp/CMakeLists.txt AzPlate.cpp-ni elle sadalayirsa VehDetach.cpp-ni de eyni yerde elave edir (GLOB ise toxunmur)."""
    p = os.path.join(SAMP, "CMakeLists.txt")
    if not os.path.isfile(p):
        return None
    t = read(p)
    if "VehDetach.cpp" in t:
        return None
    m = re.search(r'^([ \t]*)([^\s"]*?)AzPlate\.cpp[ \t]*$', t, re.M)
    if not m:
        return None
    line = "%s%sVehDetach.cpp" % (m.group(1), m.group(2).replace("AzPlate", ""))
    return p, t[:m.end()] + "\n" + line + t[m.end():]


def main():
    plan = {}   # yol -> yeni metn
    created = {}
    try:
        gui = locate("samp/gui/gui.cpp")
        veh = locate("samp/game/vehicle.cpp")
        vgh = locate("samp/game/Entity/CVehicleGTA.h")
        vgc = locate("samp/game/Entity/CVehicleGTA.cpp")
        plan[gui] = patch_gui(read(gui))
        plan[veh] = patch_vehicle_cpp(read(veh))
        plan[vgh] = patch_vehgta_h(read(vgh))
        plan[vgc] = patch_vehgta_cpp(read(vgc))
        cm = patch_cmake()
        if cm:
            plan[cm[0]] = cm[1]
        gdir = os.path.join(SAMP, "game")
        created[os.path.join(gdir, "VehDetach.h")] = VEHDETACH_H
        created[os.path.join(gdir, "VehDetach.cpp")] = VEHDETACH_CPP
    except PatchError as e:
        print("XETA: %s" % e)
        print("Hec bir fayl deyismedi.")
        sys.exit(1)

    for p, new in plan.items():
        shutil.copyfile(p, p + ".bak_v7")
        with open(p, "w", encoding="utf-8", newline="") as f:
            f.write(new)
        print("patch: %s" % os.path.relpath(p, ROOT))
    for p, new in created.items():
        with open(p, "w", encoding="utf-8", newline="") as f:
            f.write(new)
        print("yeni : %s" % os.path.relpath(p, ROOT))
    print("\nHazirdir. Qur (gradle). CMake-de fayllar GLOB ile yigilmirsa samp/CMakeLists.txt-ə game/VehDetach.cpp elave et.")


if __name__ == "__main__":
    main()
