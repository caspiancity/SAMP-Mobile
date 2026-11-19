#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_v8.py  --  layihenin kokunde:   python3 apply_v8.py
v7 tetbiq olunubsa ONCE onu geri qaytarir (*.bak_v7), sonra v8-i temiz fayllara tetbiq edir.
Hamisi evvel yoxlanilir: bir anchor tapilmasa HEC BIR FAYL deyismir. Yedek: <fayl>.bak_v8

v8:
  * Nitro / Hidravlika / Stereo / butun komponentler: SA-MP-in standart yolu (request -> load -> add), native zeng YOXDUR (atma sebebi bu idi)
  * Handling: MTA tipli (ad + deyer qutusu + bar + - / +), rəqəm MENYU DAXILINDE (chata kecmir)
  * Handling backend: handling pointeri birbasa CVehicleGTA::m_pHandlingData-dan (xam offset yox), ferdi kopya,
      kutle (CPhysical), maxspeed (vitesler) duzgun tetbiq olunur
  * Tuning: sol kateqoriya siyahisi: Boya, Disk, Kapot, Spoyler, Dam, Yan perde, Bufer(on/arxa), Ekzoz, Hava girisi, Elave,
      Nitro/Hidravlika, Detallari sok, Far rengi, Nomre. Duz ag-qara-boz dizayn.
  * Detallar: yalniz modelde REAL movcud olanlar gosterilir (menasiz "yoxdur" yazilari yoxdur)
  * Diaqnostika dugmesi -> SAMP/mm_diag.txt (bir sey islemese bu faylı gonder)
"""
import os, re, sys, shutil

ROOT = os.environ.get("ROOT", os.getcwd())
SAMP = os.path.join(ROOT, "app", "src", "main", "cpp", "samp")
TAG = "MM_V8"


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


def read_clean(p):
    """v7 patch olunubsa yedekden temiz variantı qaytarir."""
    t = read(p)
    bak = p + ".bak_v7"
    if "MM_V7" in t and os.path.isfile(bak):
        return read(bak), True
    return t, False


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



GUI_COL = r'''static inline ImU32 Col(int r, int g, int b, int a = 255)
        {   // MM_V8: ag-qara-boz tema: az doymus (boz-mavi) tonlar saf boza cevrilir; real rengler (qirmizi, yasil...) qalir
                const int mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
                if (mx - mn <= 40) { const int l = (r * 30 + g * 59 + b * 11) / 100; r = g = b = l; }
                return IM_COL32(r, g, b, (int)((float)a * s_A));
        }
        // deqiq reng (swatch / onizleme) - mono suzgecsiz
        static inline ImU32 ColT(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, (int)((float)a * s_A)); }
        static inline ImU32 Accent(float /*istifade olunmur*/, int a = 255) { return Col(128, 128, 128, a); }'''

GUI_SMALLBTN = r'''Fill(d, primary ? Col(238, 238, 238, 225 + (int)(30.0f * p)) : Col(255, 255, 255, 20 + (int)(30.0f * p)), S(12.0f));
        TxtC(S(15.0f), d, primary ? Col(12, 12, 12) : Col(244, 244, 244), label, true);'''

GUI_GLASS = r'''static void Glass(const mm::Rc& P, float rad)
    {   // MM_V8: duz qara panel (shuseli/qradient yoxdur)
        using namespace mm;
        (void)rad;
        const float r = S(10.0f);
        Fill(Rc{ P.x - S(2.0f), P.y - S(2.0f), P.w + S(4.0f), P.h + S(4.0f) }, Col(0, 0, 0, 90), r + S(2.0f));
        Fill(P, Col(12, 12, 12, 250), r);
        Stroke(P, Col(255, 255, 255, 46), r, S(1.0f));
    }

    '''

GUI_TABS = r'''// segmentli idareetme (MM_V8: 4 esas bolme; Tuning-in sol kateqoriya siyahisi)
    static const char* kTabs[4] = { "Maşın", "Handling", "Tuning", "Spidometr" };
    static const char* kRail[15] = { "Boya", "Disk və təkər", "Kapot", "Spoyler", "Dam", "Yan pərdə", "Ön bufer", "Arxa bufer",
                                     "Ekzoz", "Hava girişi", "Əlavə", "Nitro / Hidravlika", "Detalları sök", "Far rəngi", "Nömrə" };
    static int s_rail = 0;
    {
        const Rc seg{ P.x + S(24.0f), P.y + S(66.0f), P.w - S(48.0f), S(40.0f) };
        Fill(seg, Col(255, 255, 255, 18), S(10.0f));
        const float tw = seg.w / 4.0f;
        s_pill = Smooth(s_pill, (float)s_tab, dt, 16.0f);
        Fill(Rc{ seg.x + S(3.0f) + s_pill * tw, seg.y + S(3.0f), tw - S(6.0f), seg.h - S(6.0f) }, Col(255, 255, 255, 50), S(8.0f));
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
            const float rx = P.x + S(24.0f), ry = P.y + S(124.0f), rw = S(194.0f), rh = S(27.0f);
            Fill(Rc{ rx - S(4.0f), ry - S(4.0f), rw + S(8.0f), rh * 15.0f + S(8.0f) }, Col(255, 255, 255, 8), S(8.0f));
            for (int i = 0; i < 15; ++i) {
                const Rc it{ rx, ry + rh * (float)i, rw, rh };
                if (Tap(200 + i, it, canInput) && s_rail != i) { s_rail = i; s_tabAnim = 0.0f; s_tabDir = 1.0f; }
                const bool on = (s_rail == i);
                if (on) {
                    Fill(it, Col(255, 255, 255, 26), S(6.0f));
                    Fill(Rc{ it.x, it.y + S(5.0f), S(3.0f), it.h - S(10.0f) }, Col(240, 240, 240), S(1.5f));
                }
                Txt(S(14.0f), it.x + S(12.0f), it.y + (it.h - S(14.0f)) * 0.5f, on ? Col(248, 248, 248) : Col(160, 160, 160), kRail[i]);
            }
        }
    }
    // bolme -> sehife:  0 Masin, 1 Handling, 3 Tuning(kateqoriyalar), 7 Detallar, 2 Far rengi, 4 Nomre, 5 Spidometr
    const int pg = (s_tab == 2) ? (s_rail == 12 ? 7 : (s_rail == 13 ? 2 : (s_rail == 14 ? 4 : 3))) : ((s_tab == 3) ? 5 : s_tab);

    '''

GUI_CX0 = r'''float cx0 = P.x + S(24.0f) + (1.0f - pe) * S(26.0f) * s_tabDir;'''
GUI_CW = r'''float cw  = P.w - S(48.0f);
    if (s_tab == 2) { cx0 += S(206.0f); cw -= S(206.0f); }   // MM_V8: sol kateqoriya siyahisi yeri'''

GUI_HANDLING = r'''else if (s_tab == 1)                                    // HANDLING (MM_V8)
    {
        mm3::Ctx cc{ cx0, y0, cw, dt, inputPage, noVeh, pVeh, gtaVeh, P, s_rail };
        mm3::PageHandling(cc, s_slot);
    }
    '''

GUI_TUNING = r'''else if (pg == 3)                                       // TUNING (MM_V8)
    {
        mm3::Ctx cc{ cx0, y0, cw, dt, inputPage, noVeh, pVeh, gtaVeh, P, s_rail };
        mm3::PageTuning(cc);
    }
    else if (pg == 7)                                       // DETALLARI SOK
    {
        mm3::Ctx cc{ cx0, y0, cw, dt, inputPage, noVeh, pVeh, gtaVeh, P, s_rail };
        mm3::PageDetach(cc);
    }
    '''

VEH_ADDCOMP = r'''#include <unistd.h>
void CVehicle::AddComponent(int iComponentID)
{
    if (!m_pVehicle || !GamePool_Vehicle_GetAt(m_dwGTAId)) return;
    if (GetVehicleSubtype() != VEHICLE_SUBTYPE_CAR) return;
    if (iComponentID < 1000 || iComponentID > 1193) return;
    if (m_pVehicle->HasUpgrade(iComponentID)) return;

    // MM_V8: SA-MP-in standart yolu. Evvelki versiyada native AddVehicleUpgrade cagirilirdi -> atma verirdi.
    ScriptCommand(&request_car_component, iComponentID);
    ScriptCommand(&load_requested_models);
    int wait = 0;
    while (!ScriptCommand(&is_component_available, iComponentID)) {
        usleep(2000);
        if (++wait > 400) return;      // ~0.8 san
    }
    uint32_t dwRet = 0;
    ScriptCommand(&add_car_component, m_dwGTAId, iComponentID, &dwRet);
}

'''

VEH_PHYS = r'''static std::map<CVehicleGTA*, std::vector<uint8_t>> g_HndxOrig;

// MM_V8: ferdi handling kopyasi + CPhysical kutlesi (handling-den AYRI saxlanir)
struct HndxPhys { float mass; float turn; uint32_t ctime; };
static std::map<CVehicleGTA*, HndxPhys> g_HndxPhys;
struct HndxOwn { uint32_t ctime; tHandlingData* copy; };
static std::map<CVehicleGTA*, HndxOwn> g_HndxOwnMap;

// maxspeed: yalniz bir sahe yox, vites cedveli ve maxVelocity de miqyaslanmalidir
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

VEH_SNAP = r'''static tHandlingData* HndxOwnCopy(CVehicleGTA* g)
{   // MM_V8: pointer birbasa strukturdan (xam offset yox); masina ferdi kopya
    if (!g || !g->m_pHandlingData) return nullptr;
    std::map<CVehicleGTA*, HndxOwn>::iterator it = g_HndxOwnMap.find(g);
    if (it != g_HndxOwnMap.end()) {
        if (it->second.ctime == g->m_nCreationTime && g->m_pHandlingData == it->second.copy) return it->second.copy;
        if (it->second.ctime != g->m_nCreationTime) delete it->second.copy;   // eyni adresde basqa masin
        g_HndxOwnMap.erase(it);
    }
    tHandlingData* c = new tHandlingData(*g->m_pHandlingData);
    g->m_pHandlingData = c;
    HndxOwn o; o.ctime = g->m_nCreationTime; o.copy = c;
    g_HndxOwnMap[g] = o;
    return c;
}

static void HndxSnap(CVehicle* v)
{
    CVehicleGTA* g = v->m_pVehicle;
    if (!g || !g->m_pHandlingData) return;
    std::map<CVehicleGTA*, HndxPhys>::iterator ph = g_HndxPhys.find(g);
    if (ph != g_HndxPhys.end() && ph->second.ctime == g->m_nCreationTime && g_HndxOrig.count(g)) return;
    g_HndxOrig[g].assign((uint8_t*)g->m_pHandlingData, (uint8_t*)g->m_pHandlingData + HND_SIZE);
    HndxPhys pp; pp.mass = g->m_fMass; pp.turn = g->m_fTurnMass; pp.ctime = g->m_nCreationTime;
    g_HndxPhys[g] = pp;
}

'''

VEH_API = r'''bool HndExt_Get(CVehicle* v, const char* name, float* out)
{
    if (!v || !v->m_pVehicle || !out) return false;
    const HndxField* f = HndxFind(name);
    if (!f) return false;
    tHandlingData* h = v->m_pVehicle->m_pHandlingData;
    if (!h) return false;
    *out = *(float*)((uintptr_t)h + f->off);
    return true;
}

bool HndExt_Set(CVehicle* v, const char* name, float val)
{
    if (!v || !v->m_pVehicle) return false;
    if (!std::isfinite(val) || val > 1.0e6f || val < -1.0e6f) return false;
    const HndxField* f = HndxFind(name);
    if (!f) return false;
    HndxSnap(v);
    tHandlingData* h = HndxOwnCopy(v->m_pVehicle);
    if (!h) return false;
    const uintptr_t p = (uintptr_t)h;
    CVehicleGTA* gv = v->m_pVehicle;
    const float oldVal = *(float*)(p + f->off);
    *(float*)(p + f->off) = val;
    if (f->off == offsetof(tHandlingData, m_fMass)) {
        HndxFixMass(p);
        if (oldVal > 1.0e-4f) gv->m_fMass *= (val / oldVal);
    }
    if (f->off == offsetof(tHandlingData, m_fTurnMass) && oldVal > 1.0e-4f) gv->m_fTurnMass *= (val / oldVal);
    if (f->off == HND_MAX_VELOCITY && oldVal > 1.0e-4f && val > 0.0f) HndxScaleSpeed(p, val / oldVal);
    if (f->off == offsetof(tHandlingData, m_fSuspensionUpperLimit) ||
        f->off == offsetof(tHandlingData, m_fSuspensionLowerLimit) ||
        f->off == offsetof(tHandlingData, m_fSuspensionBiasBetweenFrontAndRear))
        gv->RefreshSuspensionLines();
    return true;
}

bool HndExt_Reset(CVehicle* v)
{
    if (!v || !v->m_pVehicle) return false;
    std::map<CVehicleGTA*, std::vector<uint8_t>>::iterator it = g_HndxOrig.find(v->m_pVehicle);
    if (it == g_HndxOrig.end() || it->second.size() != HND_SIZE) return false;
    tHandlingData* h = HndxOwnCopy(v->m_pVehicle);
    if (!h) return false;
    memcpy((void*)((uintptr_t)h + 4), it->second.data() + 4, HND_SIZE - 4);
    std::map<CVehicleGTA*, HndxPhys>::iterator ph = g_HndxPhys.find(v->m_pVehicle);
    if (ph != g_HndxPhys.end()) { v->m_pVehicle->m_fMass = ph->second.mass; v->m_pVehicle->m_fTurnMass = ph->second.turn; }
    v->m_pVehicle->RefreshSuspensionLines();
    return true;
}

'''

H_DECL = r'''void RemoveVehicleUpgrade(int32 upgradeModelIndex);
    // MM_V8
    bool HasUpgrade(int32 modelId) const { for (int i = 0; i < 15; ++i) if (m_anUpgrades[i] == modelId) return true; return false; }
    void RefreshSuspensionLines();'''

CPP_DEF = r'''CHook::CallFunction<void>(g_libGTASA + (VER_x32 ? 0x58CC2C + 1 : 0x6B0718), this, upgradeModelIndex);
}

// MM_V8: susp (hundurluk) handling deyisenden sonra xetlerin yeniden qurulmasi. Simvol tapilmasa HEC NE etmir (atma yoxdur).
#include <dlfcn.h>
void CVehicleGTA::RefreshSuspensionLines() {
    if (!m_pHandlingData) return;
    if (m_pHandlingData->m_bIsBike || m_pHandlingData->m_bIsHeli || m_pHandlingData->m_bIsPlane || m_pHandlingData->m_bIsBoat) return;
    static void (*fn)(CVehicleGTA*) = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        void* h = dlopen("libGTASA.so", RTLD_NOLOAD | RTLD_LAZY);
        if (h) fn = (void (*)(CVehicleGTA*))dlsym(h, "_ZN11CAutomobile20SetupSuspensionLinesEv");
    }
    if (fn) fn(this);
}'''

MM3 = r'''// ===== MM_V8: handling (MTA tipli), qaraj tuning, detallar =====
#include "../game/VehDetach.h"
#include <set>

namespace mm3
{
    struct Ctx {
        float cx0, y0, cw, dt;
        bool  in, noVeh;
        CVehicle*    veh;
        CVehicleGTA* g;
        mm::Rc       P;
        int          rail;
    };

    // ------------------------------------------------------------ umumi elementlər
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
        Fill(r, Col(255, 255, 255, hot ? 70 : 30), S(6.0f));
        const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f, k = S(5.0f);
        s_dl->AddLine(ImVec2(cx - k, cy), ImVec2(cx + k, cy), Col(240, 240, 240, 240), S(2.0f));
        if (plus) s_dl->AddLine(ImVec2(cx, cy - k), ImVec2(cx, cy + k), Col(240, 240, 240, 240), S(2.0f));
    }

    static int AdjRow(int id, const mm::Rc& row, const char* label, const char* val, float dt, bool can)
    {
        using namespace mm;
        Fill(row, Col(255, 255, 255, 14), S(8.0f));
        Txt(S(15.0f), row.x + S(12.0f), row.y + (row.h - S(15.0f)) * 0.5f, Col(228, 228, 228), label);
        const float bh = row.h - S(6.0f), bw = S(34.0f), y = row.y + S(3.0f);
        const Rc pl{ row.x + row.w - bw - S(3.0f), y, bw, bh };
        const Rc vr{ pl.x - S(6.0f) - S(96.0f), y, S(96.0f), bh };
        const Rc mn{ vr.x - S(6.0f) - bw, y, bw, bh };
        int n = 0;
        n -= Repeat(id, mn, dt, can);
        n += Repeat(id + 1, pl, dt, can);
        Glyph(mn, false, s_act == id);
        Glyph(pl, true,  s_act == id + 1);
        Fill(vr, Col(0, 0, 0, 100), S(6.0f));
        TxtC(S(15.0f), vr, Col(255, 255, 255), val, true);
        return n;
    }

    static bool Tile(int id, const mm::Rc& r, const char* label, bool on, bool dim, bool can)
    {
        using namespace mm;
        const bool hit = Tap(id, r, can);
        Fill(r, on ? Col(240, 240, 240) : Col(255, 255, 255, s_act == id ? 42 : 18), S(7.0f));
        TxtC(S(14.0f), r, on ? Col(12, 12, 12) : (dim ? Col(96, 96, 96) : Col(222, 222, 222)), label, on);
        return hit;
    }

    static bool Chip(int id, const mm::Rc& r, const char* label, const char* sub, bool on, float dt, bool can, int anim)
    {
        using namespace mm;
        static float s_a[32] = {};
        const bool hit = Tap(id, r, can);
        float& a = s_a[anim & 31];
        a = Smooth(a, on ? 1.0f : 0.0f, dt, 16.0f);
        Fill(r, Col(255, 255, 255, s_act == id ? 26 : 14), S(8.0f));
        Txt(S(16.0f), r.x + S(12.0f), r.y + S(5.0f), Col(238, 238, 238), label);
        Txt(S(12.0f), r.x + S(12.0f), r.y + r.h - S(19.0f), Col(132, 132, 132), sub);
        const Rc tr{ r.x + r.w - S(54.0f), r.y + (r.h - S(24.0f)) * 0.5f, S(42.0f), S(24.0f) };
        const int lv = (int)Lerp(70.0f, 240.0f, a);
        Fill(tr, Col(lv, lv, lv), S(12.0f));
        Dot(tr.x + S(12.0f) + a * (tr.w - S(24.0f)), tr.y + tr.h * 0.5f, S(9.0f), on ? Col(20, 20, 20) : Col(200, 200, 200));
        return hit;
    }

    static void NoVeh(const Ctx& c, const char* sub)
    {
        mm2::EmptyState(mm::Rc{ c.cx0, c.y0, c.cw, mm::S(300.0f) }, "Avtomobildə deyilsiniz", sub);
    }

    static void DrawMsg(float x, float y)
    {
        using namespace mm;
        if (mm2::s_msgT > 0.01f) Txt(S(13.0f), x, y, Col(120, 215, 160, (int)(255.0f * mm2::s_msgT)), mm2::s_msg);
    }

    // ------------------------------------------------------------ diaqnostika (fayla yazir)
    static void WriteDiag(const Ctx& c)
    {
        if (!g_pszStorage || !c.g || !c.veh) { mm2::SetMsg("Diaqnostika mümkün deyil"); return; }
        char p[0x100];
        snprintf(p, sizeof(p), "%sSAMP/mm_diag.txt", g_pszStorage);
        FILE* f = fopen(p, "w");
        if (!f) { mm2::SetMsg("Fayl yazıla bilmədi"); return; }
        CVehicleGTA* g = c.g;
        fprintf(f, "model=%d veh=%p creation=%u subtype=%d\n", (int)g->m_nModelIndex, (void*)g, (unsigned)g->m_nCreationTime, c.veh->GetVehicleSubtype());
        float mass = 0.0f, acc = 0.0f, mx = 0.0f;
        const bool ok = HndExt_Get(c.veh, "mass", &mass);
        HndExt_Get(c.veh, "accel", &acc);
        HndExt_Get(c.veh, "maxspeed", &mx);
        fprintf(f, "handling ptr=%p get(mass)=%d %.3f direct=%.3f CPhysical.mass=%.3f accel=%.4f maxspeed=%.4f\n",
                (void*)g->m_pHandlingData, ok ? 1 : 0, mass, g->m_pHandlingData ? g->m_pHandlingData->m_fMass : -1.0f, g->m_fMass, acc, mx);
        fprintf(f, "upgrades:");
        for (int i = 0; i < 15; ++i) fprintf(f, " %d", (int)g->m_anUpgrades[i]);
        fprintf(f, "\nnitroBoosts=%d wheelScale=%.3f\n", (int)g->m_nNitroBoosts, g->m_wheelScale);
        VehDetach::Diag(g, f);
        fclose(f);
        mm2::SetMsg("Yazıldı: SAMP/mm_diag.txt");
    }

    // ============================================================ HANDLING (MTA tipli: sətir = ad + dəyər + bar)
    struct HGrp { const char* name; int n; const char* keys[8]; };
    static const HGrp kHG[5] = {
        { "Kütlə",         5, { "mass", "turnmass", "drag", "collision", "buoyancy" } },
        { "Mühərrik",      4, { "accel", "maxspeed", "brake", "brakebias" } },
        { "Sükan / yol",   4, { "steering", "traction", "tractionloss", "tractionbias" } },
        { "Süspansiyon",   7, { "suspforce", "suspdamp", "suspdampspeed", "suspupper", "susplower", "suspbias", "suspantidive" } },
        { "Mərkəz",        4, { "comx", "comy", "comz", "seatoffset" } },
    };

    static int HIdx(const char* key)
    {
        for (int i = 0; i < mm2::kHFN; ++i) if (!strcmp(mm2::kHF[i].key, key)) return i;
        return -1;
    }

    static void HRange(const char* key, float base, float& lo, float& hi)
    {
        if (!strncmp(key, "com", 3)) { lo = -2.0f; hi = 2.0f; }
        else if (fabsf(base) < 1.0e-4f) { lo = -1.0f; hi = 1.0f; }
        else { const float a = base * 0.25f, b = base * 3.0f; lo = a < b ? a : b; hi = a < b ? b : a; }
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
        static int  s_grp = 0, s_edit = -1;
        static char s_buf[20] = "";
        static CVehicleGTA* s_bv = nullptr;
        static float s_base[32];
        if (c.noVeh || !c.veh || !c.g) {
            s_edit = -1; s_bv = nullptr;
            NoVeh(c, "Handling yalnız maşının içində dəyişdirilir");
            return;
        }
        CVehicle* pVeh = c.veh;
        const float dt = c.dt;
        const bool lin = c.in && s_edit < 0;

        if (c.g != s_bv) {                       // maşın dəyişdi / sıfırlandı: orijinal dəyərləri yadda saxla
            s_bv = c.g;
            s_edit = -1;
            for (int i = 0; i < mm2::kHFN && i < 32; ++i) {
                float cv = 0.0f;
                s_base[i] = HndExt_Get(pVeh, mm2::kHF[i].key, &cv) ? cv : 0.0f;
            }
        }

        // 1) slot + fayl
        for (int i = 0; i < 5; ++i) {
            const Rc b{ c.cx0 + S(40.0f) * i, c.y0, S(36.0f), S(30.0f) };
            if (Tap(80 + i, b, lin)) slot = i + 1;
            const bool on = (slot == i + 1);
            Fill(b, on ? Col(238, 238, 238) : Col(255, 255, 255, 18), S(8.0f));
            char nb[4];
            snprintf(nb, sizeof(nb), "%d", i + 1);
            TxtC(S(15.0f), b, on ? Col(12, 12, 12) : Col(200, 200, 200), nb, true);
            if (HndExt_SlotUsed(i + 1)) Dot(b.x + b.w - S(6.0f), b.y + S(6.0f), S(3.0f), Col(80, 220, 140));
        }
        static const char* const kAct[3] = { "Fayl seç", "Tətbiq et", "Sıfırla" };
        {
            const float bx = c.cx0 + S(208.0f), gap = S(8.0f);
            const float bw = (c.cw - S(208.0f) - gap * 2.0f) / 3.0f;
            for (int k = 0; k < 3; ++k) {
                const Rc b{ bx + k * (bw + gap), c.y0, bw, S(30.0f) };
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
                    s_bv = nullptr;
                } else {
                    mm2::SetMsg(HndExt_Reset(pVeh) ? "Standarta qaytarıldı" : "Dəyişiklik yoxdur");
                    s_bv = nullptr;
                }
            }
        }

        // 2) qruplar
        {
            const float dgw = S(112.0f);
            const float pw = (c.cw - dgw - S(8.0f) - S(4.0f) * 4.0f) / 5.0f;
            for (int i = 0; i < 5; ++i) {
                const Rc b{ c.cx0 + (pw + S(4.0f)) * (float)i, c.y0 + S(36.0f), pw, S(26.0f) };
                if (Tap(310 + i, b, lin)) s_grp = i;
                const bool on = (s_grp == i);
                Fill(b, on ? Col(238, 238, 238) : Col(255, 255, 255, 16), S(8.0f));
                TxtC(S(13.0f), b, on ? Col(12, 12, 12) : Col(190, 190, 190), kHG[i].name, on);
            }
            if (mm2::SmallBtn(316, Rc{ c.cx0 + c.cw - dgw, c.y0 + S(36.0f), dgw, S(26.0f) }, "Diaqnostika", dt, lin, false)) WriteDiag(c);
        }
        DrawMsg(c.cx0 + S(240.0f), c.y0 + S(8.0f));

        // 3) sətirlər
        const HGrp& G = kHG[s_grp];
        const float ry0 = c.y0 + S(68.0f), rh = S(48.0f), rstep = S(52.0f);
        const MMTouch& t = g_mmTouch;
        for (int k = 0; k < G.n; ++k) {
            const char* key = G.keys[k];
            const int r = HIdx(key);
            if (r < 0) continue;
            const Rc row{ c.cx0, ry0 + rstep * (float)k, c.cw, rh };
            float cur = 0.0f;
            const bool have = HndExt_Get(pVeh, key, &cur);

            const float by = row.y + S(37.0f);
            const Rc mn{ row.x + S(8.0f), by - S(9.0f), S(30.0f), S(18.0f) };
            const Rc pl{ row.x + row.w - S(38.0f), by - S(9.0f), S(30.0f), S(18.0f) };
            const float tx0 = row.x + S(48.0f), tx1 = row.x + row.w - S(48.0f);
            const Rc hit{ tx0 - S(10.0f), row.y + S(24.0f), (tx1 - tx0) + S(20.0f), S(24.0f) };
            const int idBase = 500 + r * 4;

            float lo, hi;
            HRange(key, s_base[r], lo, hi);

            if (lin && have) {
                const int n = Repeat(idBase + 1, pl, dt, true) - Repeat(idBase, mn, dt, true);
                if (n != 0) {
                    float v = cur;
                    const int cnt = n < 0 ? -n : n;
                    for (int q = 0; q < cnt; ++q) v += (n < 0 ? -1.0f : 1.0f) * HStep(key, v);
                    if (strncmp(key, "com", 3) != 0 && v < 0.0f) v = 0.0f;
                    if (!strcmp(key, "mass") && v < 1.0f) v = 1.0f;
                    HndExt_Set(pVeh, key, v);
                }
                if (t.pressed && s_act == 0 && In(hit, t.x, t.y)) s_act = idBase + 2;
                if (s_act == idBase + 2 && t.down) {
                    const float f = Clamp((t.x - tx0) / (tx1 - tx0), 0.0f, 1.0f);
                    HndExt_Set(pVeh, key, lo + (hi - lo) * f);
                }
                HndExt_Get(pVeh, key, &cur);
            }

            // çəkim
            Fill(row, Col(255, 255, 255, 12), S(8.0f));
            Txt(S(15.0f), row.x + S(10.0f), row.y + S(4.0f), Col(232, 232, 232), mm2::kHF[r].label);
            {
                char ob[40], ob2[64];
                snprintf(ob, sizeof(ob), mm2::kHF[r].fmt, s_base[r]);
                snprintf(ob2, sizeof(ob2), "orijinal %s", ob);
                Txt(S(11.0f), row.x + S(10.0f) + TW(S(15.0f), mm2::kHF[r].label) + S(10.0f), row.y + S(8.0f), Col(112, 112, 112), ob2);
            }
            char vb[32] = "-";
            if (have) snprintf(vb, sizeof(vb), mm2::kHF[r].fmt, cur);
            const Rc vbx{ row.x + row.w - S(124.0f), row.y + S(3.0f), S(114.0f), S(24.0f) };
            if (lin && have && Tap(idBase + 3, vbx, true)) { s_edit = r; snprintf(s_buf, sizeof(s_buf), "%.5g", cur); }
            Fill(vbx, Col(0, 0, 0, 130), S(6.0f));
            Stroke(vbx, Col(255, 255, 255, 60), S(6.0f), S(1.0f));
            TxtC(S(14.0f), vbx, Col(255, 255, 255), vb, true);

            Glyph(mn, false, s_act == idBase);
            Glyph(pl, true,  s_act == idBase + 1);
            Fill(Rc{ tx0, by - S(3.0f), tx1 - tx0, S(6.0f) }, Col(255, 255, 255, 40), S(3.0f));
            const float f = (hi > lo) ? Clamp((cur - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
            if (have) {
                Fill(Rc{ tx0, by - S(3.0f), (tx1 - tx0) * f, S(6.0f) }, Col(235, 235, 235), S(3.0f));
                Dot(tx0 + (tx1 - tx0) * f, by, s_act == idBase + 2 ? S(10.0f) : S(8.0f), Col(255, 255, 255));
            }
        }

        // 4) menyu daxili rəqəm paneli
        if (s_edit >= 0) {
            Fill(c.P, Col(0, 0, 0, 150), S(10.0f));
            const Rc np{ c.P.x + (c.P.w - S(300.0f)) * 0.5f, c.P.y + S(112.0f), S(300.0f), S(344.0f) };
            Fill(np, Col(20, 20, 20, 252), S(12.0f));
            Stroke(np, Col(255, 255, 255, 60), S(12.0f), S(1.0f));
            TxtB(S(16.0f), np.x + S(16.0f), np.y + S(12.0f), Col(240, 240, 240), mm2::kHF[s_edit].label);
            const Rc fld{ np.x + S(16.0f), np.y + S(40.0f), np.w - S(32.0f), S(46.0f) };
            Fill(fld, Col(0, 0, 0, 140), S(8.0f));
            std::string shown = s_buf;
            shown += "|";
            TxtC(S(22.0f), fld, Col(255, 255, 255), shown.c_str(), true);

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
                    const float fv = strtof(s_buf, &end);
                    if (s_buf[0] == 0 || end == s_buf) mm2::SetMsg("Yanlış dəyər");
                    else if (HndExt_Set(pVeh, mm2::kHF[s_edit].key, fv)) mm2::SetMsg(std::string(mm2::kHF[s_edit].label) + " = " + s_buf);
                    else mm2::SetMsg("Dəyər qəbul edilmədi");
                    s_edit = -1;
                }
                break;
            }
        }
    }

    // ============================================================ TUNING: komponent cedvelleri (SA-MP standart ID-ler)
    struct PCat { const char* name; const int* ids; int n; };
#define MM_N(a) ((int)(sizeof(a) / sizeof((a)[0])))
    static const int kHood[]    = { 1004, 1005, 1011, 1012 };
    static const int kSpoiler[] = { 1000, 1001, 1002, 1003, 1014, 1015, 1016, 1023, 1049, 1050, 1058, 1060, 1138, 1139, 1146, 1147, 1158, 1162, 1163, 1164 };
    static const int kRoof[]    = { 1006, 1032, 1033, 1035, 1038, 1053, 1054, 1055, 1061, 1067, 1068, 1088, 1091, 1103, 1128, 1130 };
    static const int kSkirt[]   = { 1007, 1017, 1026, 1027, 1030, 1031, 1036, 1039, 1040, 1041, 1042, 1047, 1048, 1051, 1052, 1056, 1057, 1062, 1063, 1069, 1070, 1071, 1072, 1090, 1093, 1094, 1095, 1099, 1101, 1102, 1106, 1107, 1108, 1118, 1119, 1120, 1121, 1122, 1124, 1133, 1134, 1137 };
    static const int kFront[]   = { 1117, 1152, 1153, 1155, 1157, 1160, 1165, 1166, 1169, 1170, 1171, 1172, 1173, 1174, 1175, 1179, 1181, 1182, 1185, 1188, 1189, 1191 };
    static const int kRear[]    = { 1140, 1141, 1148, 1149, 1150, 1151, 1154, 1156, 1159, 1161, 1167, 1168, 1176, 1177, 1178, 1180, 1183, 1184, 1186, 1187, 1190, 1192, 1193 };
    static const int kExh[]     = { 1018, 1019, 1020, 1021, 1022, 1028, 1029, 1034, 1037, 1043, 1044, 1045, 1046, 1059, 1064, 1065, 1066, 1089, 1092, 1104, 1105, 1113, 1114, 1126, 1127, 1129, 1132, 1135, 1136 };
    static const int kVent[]    = { 1142, 1143, 1144, 1145 };
    static const int kExtra[]   = { 1013, 1024, 1100, 1109, 1110, 1115, 1116, 1123, 1125 };
    static const int kWid[17]   = { 1025, 1073, 1074, 1075, 1076, 1077, 1078, 1079, 1080, 1081, 1082, 1083, 1084, 1085, 1096, 1097, 1098 };
    static const char* const kWn[17] = { "Offroad", "Shadow", "Mega", "Rimshine", "Wires", "Classic", "Twist", "Cutter", "Switch", "Grove", "Import", "Dollar", "Trance", "Atomic", "Ahab", "Virtual", "Access" };
    static const int kNitro[3]  = { 1009, 1008, 1010 };
    static const int kHyd[1]    = { 1087 };
    static const int kStereo[1] = { 1086 };

    static const PCat kPC[9] = {
        { "Kapot", kHood, MM_N(kHood) }, { "Spoyler", kSpoiler, MM_N(kSpoiler) }, { "Dam", kRoof, MM_N(kRoof) },
        { "Yan pərdə", kSkirt, MM_N(kSkirt) }, { "Ön bufer", kFront, MM_N(kFront) }, { "Arxa bufer", kRear, MM_N(kRear) },
        { "Ekzoz", kExh, MM_N(kExh) }, { "Hava girişi", kVent, MM_N(kVent) }, { "Əlavə (duman fara, bullbar)", kExtra, MM_N(kExtra) },
    };

    static std::set<int> s_fail;
    static int s_failModel = -1;

    static int CurrentIn(CVehicleGTA* g, const int* ids, int n)
    {
        for (int i = 0; i < n; ++i) if (g->HasUpgrade(ids[i])) return ids[i];
        return 0;
    }

    // eyni qrupdakini sok, yenisini qoy; olmursa evvelkini qaytar. Hamisi skript (request -> load -> add) yolu ile
    static bool Install(CVehicle* v, CVehicleGTA* g, int id, const int* ids, int n)
    {
        const int cur = CurrentIn(g, ids, n);
        if (cur == id) { v->RemoveComponent(id); mm2::SetMsg("Sökülüb"); return true; }
        if (cur) v->RemoveComponent(cur);
        v->AddComponent(id);
        if (g->HasUpgrade(id)) { mm2::SetMsg("Qoşuldu"); return true; }
        s_fail.insert(id);
        if (cur) v->AddComponent(cur);
        mm2::SetMsg("Bu maşına uyğun deyil");
        return false;
    }

    static void PagePartGrid(const Ctx& c, const PCat& pc)
    {
        using namespace mm;
        CVehicle* v = c.veh; CVehicleGTA* g = c.g;
        TxtB(S(17.0f), c.cx0, c.y0 + S(2.0f), Col(240, 240, 240), pc.name);
        DrawMsg(c.cx0 + S(240.0f), c.y0 + S(6.0f));
        const int cur = CurrentIn(g, pc.ids, pc.n);
        if (mm2::SmallBtn(520, Rc{ c.cx0 + c.cw - S(110.0f), c.y0 - S(2.0f), S(110.0f), S(28.0f) }, "Sök", c.dt, c.in && cur != 0, false) && cur) {
            v->RemoveComponent(cur);
            mm2::SetMsg("Sökülüb");
        }
        const int cols = 8;
        const float gap = S(6.0f);
        const float tw = (c.cw - gap * (float)(cols - 1)) / (float)cols, th = S(38.0f);
        for (int i = 0; i < pc.n; ++i) {
            const Rc r{ c.cx0 + (float)(i % cols) * (tw + gap), c.y0 + S(38.0f) + (float)(i / cols) * (th + gap), tw, th };
            char lb[8];
            snprintf(lb, sizeof(lb), "%d", pc.ids[i]);
            if (Tile(600 + i, r, lb, g->HasUpgrade(pc.ids[i]), s_fail.count(pc.ids[i]) != 0, c.in))
                Install(v, g, pc.ids[i], pc.ids, pc.n);
        }
    }

    static void PagePaint(const Ctx& c)
    {
        using namespace mm;
        CVehicle* v = c.veh; CVehicleGTA* g = c.g;
        TxtB(S(17.0f), c.cx0, c.y0 + S(2.0f), Col(240, 240, 240), "Boya");
        DrawMsg(c.cx0 + S(240.0f), c.y0 + S(6.0f));
        int c1 = g->m_nPrimaryColor, c2 = g->m_nSecondaryColor;
        char b[12];
        snprintf(b, sizeof(b), "%d", c1);
        int n = AdjRow(700, Rc{ c.cx0, c.y0 + S(38.0f), c.cw, S(40.0f) }, "Əsas rəng (0 - 126)", b, c.dt, c.in);
        if (n != 0) { c1 = (int)Clamp((float)(c1 + n), 0.0f, 126.0f); v->SetColor((uint8_t)c1, (uint8_t)c2); }
        snprintf(b, sizeof(b), "%d", c2);
        n = AdjRow(703, Rc{ c.cx0, c.y0 + S(86.0f), c.cw, S(40.0f) }, "İkinci rəng (0 - 126)", b, c.dt, c.in);
        if (n != 0) { c2 = (int)Clamp((float)(c2 + n), 0.0f, 126.0f); v->SetColor((uint8_t)c1, (uint8_t)c2); }
        Txt(S(14.0f), c.cx0, c.y0 + S(146.0f), Col(190, 190, 190), "Paintjob");
        for (int i = 0; i < 3; ++i) {
            char lb[8];
            snprintf(lb, sizeof(lb), "%d", i + 1);
            if (Tile(710 + i, Rc{ c.cx0 + (S(70.0f) + S(6.0f)) * (float)i, c.y0 + S(170.0f), S(70.0f), S(38.0f) }, lb, false, false, c.in)) {
                v->SetPaintJob((uint8_t)i);
                mm2::SetMsg("Paintjob tətbiq olundu (modeldə varsa)");
            }
        }
    }

    static void PageWheels(const Ctx& c)
    {
        using namespace mm;
        CVehicle* v = c.veh; CVehicleGTA* g = c.g;
        TxtB(S(17.0f), c.cx0, c.y0 + S(2.0f), Col(240, 240, 240), "Disk və təkər");
        DrawMsg(c.cx0 + S(240.0f), c.y0 + S(6.0f));
        const int cur = CurrentIn(g, kWid, 17);
        if (mm2::SmallBtn(521, Rc{ c.cx0 + c.cw - S(110.0f), c.y0 - S(2.0f), S(110.0f), S(28.0f) }, "Sök", c.dt, c.in && cur != 0, false) && cur) {
            v->RemoveComponent(cur);
            mm2::SetMsg("Sökülüb");
        }
        const int cols = 6;
        const float gap = S(6.0f);
        const float tw = (c.cw - gap * (float)(cols - 1)) / (float)cols, th = S(38.0f);
        for (int i = 0; i < 17; ++i) {
            const Rc r{ c.cx0 + (float)(i % cols) * (tw + gap), c.y0 + S(38.0f) + (float)(i / cols) * (th + gap), tw, th };
            if (Tile(650 + i, r, kWn[i], g->HasUpgrade(kWid[i]), s_fail.count(kWid[i]) != 0, c.in))
                Install(v, g, kWid[i], kWid, 17);
        }

        static CVehicleGTA* s_bv = nullptr;
        static float s_baseLow = 0.0f, s_baseUp = 0.0f;
        if (g != s_bv) {
            s_bv = g;
            float lo = 0.0f, up = 0.0f;
            s_baseLow = HndExt_Get(v, "susplower", &lo) ? lo : 0.0f;
            s_baseUp  = HndExt_Get(v, "suspupper", &up) ? up : 0.0f;
        }
        char vb[32];
        const float yr = c.y0 + S(38.0f) + 3.0f * (th + gap) + S(14.0f);
        {
            float ws = g->m_wheelScale;
            if (ws < 0.05f || ws > 5.0f) ws = 1.0f;
            snprintf(vb, sizeof(vb), "%.2f", ws);
            const int n = AdjRow(720, Rc{ c.cx0, yr, c.cw, S(40.0f) }, "Təkər ölçüsü", vb, c.dt, c.in);
            if (n != 0) g->m_wheelScale = Clamp(ws + 0.05f * (float)n, 0.5f, 2.0f);
        }
        {
            float lo = 0.0f, up = 0.0f;
            const bool ok = HndExt_Get(v, "susplower", &lo) && HndExt_Get(v, "suspupper", &up);
            snprintf(vb, sizeof(vb), ok ? "%+.2f" : "-", lo - s_baseLow);
            const int n = AdjRow(723, Rc{ c.cx0, yr + S(48.0f), c.cw, S(40.0f) }, "Maşının hündürlüyü", vb, c.dt, c.in && ok);
            if (ok && n != 0) {
                const float st = 0.02f * (float)n;
                if (fabsf((lo - s_baseLow) + st) <= 0.4f) {
                    HndExt_Set(v, "susplower", lo + st);
                    HndExt_Set(v, "suspupper", up + st);
                }
            }
        }
        if (mm2::SmallBtn(522, Rc{ c.cx0, yr + S(96.0f), S(220.0f), S(34.0f) }, "Ölçü və hündürlüyü sıfırla", c.dt, c.in, false)) {
            g->m_wheelScale = 1.0f;
            HndExt_Set(v, "susplower", s_baseLow);
            HndExt_Set(v, "suspupper", s_baseUp);
            mm2::SetMsg("Sıfırlandı");
        }
    }

    static void PageSystems(const Ctx& c)
    {
        using namespace mm;
        CVehicle* v = c.veh; CVehicleGTA* g = c.g;
        TxtB(S(17.0f), c.cx0, c.y0 + S(2.0f), Col(240, 240, 240), "Nitro / Hidravlika / Stereo");
        DrawMsg(c.cx0 + S(300.0f), c.y0 + S(6.0f));
        struct Q { int id; const char* name; const int* grp; int n; };
        static const Q kQ[5] = {
            { 1009, "Nitro x2", kNitro, 3 }, { 1008, "Nitro x5", kNitro, 3 }, { 1010, "Nitro x10", kNitro, 3 },
            { 1087, "Hidravlika", kHyd, 1 }, { 1086, "Stereo", kStereo, 1 } };
        for (int i = 0; i < 5; ++i) {
            const Rc r{ c.cx0, c.y0 + S(38.0f) + (S(50.0f) + S(6.0f)) * (float)i, c.cw, S(50.0f) };
            const bool has = g->HasUpgrade(kQ[i].id);
            char sub[24];
            snprintf(sub, sizeof(sub), "ID %d", kQ[i].id);
            if (Chip(390 + i, r, kQ[i].name, has ? "Qoşulub" : sub, has, c.dt, c.in, 16 + i))
                Install(v, g, kQ[i].id, kQ[i].grp, kQ[i].n);
        }
    }

    static void PageTuning(const Ctx& c)
    {
        if (c.noVeh || !c.veh || !c.g) { NoVeh(c, "Tuning yalnız maşının içində işləyir"); return; }
        if (s_failModel != (int)c.g->m_nModelIndex) { s_failModel = (int)c.g->m_nModelIndex; s_fail.clear(); }
        switch (c.rail) {
            case 0:  PagePaint(c); break;
            case 1:  PageWheels(c); break;
            case 11: PageSystems(c); break;
            default: if (c.rail >= 2 && c.rail <= 10) PagePartGrid(c, kPC[c.rail - 2]); break;
        }
    }

    // ============================================================ DETALLAR (sokme)
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
        TxtB(S(17.0f), c.cx0, c.y0 + S(2.0f), Col(240, 240, 240), "Detalları sök");
        DrawMsg(c.cx0 + S(200.0f), c.y0 + S(6.0f));
        if (mm2::SmallBtn(420, Rc{ c.cx0 + c.cw - S(250.0f), c.y0 - S(2.0f), S(120.0f), S(28.0f) }, "Hamısını sök", dt, c.in, false)) VehDetach::HideAll(g);
        if (mm2::SmallBtn(421, Rc{ c.cx0 + c.cw - S(124.0f), c.y0 - S(2.0f), S(124.0f), S(28.0f) }, "Hamısını qaytar", dt, c.in, true)) VehDetach::ShowAll(g);

        int shown = 0;
        const float colW = (c.cw - S(8.0f)) * 0.5f, rh = S(44.0f), rg = S(6.0f);
        for (int p = 0; p < VehDetach::P_COUNT; ++p) {
            if (!((s_mask >> p) & 1u)) continue;
            const int col = shown / 8, ri = shown % 8;
            ++shown;
            const Rc r{ c.cx0 + (float)col * (colW + S(8.0f)), c.y0 + S(38.0f) + (rh + rg) * (float)ri, colW, rh };
            const bool hid = VehDetach::IsHidden(g, p);
            if (Chip(430 + p, r, VehDetach::kLabel[p], hid ? "Sökülüb" : "Yerindədir", hid, dt, c.in, p)) VehDetach::Toggle(g, p);
        }
        if (shown == 0)
            Txt(S(14.0f), c.cx0, c.y0 + S(46.0f), Col(170, 170, 170), "Bu modeldə sökülə bilən ayrıca detal tapılmadı. Diaqnostika düyməsini bas.");
        if (mm2::SmallBtn(423, Rc{ c.cx0 + c.cw - S(130.0f), c.y0 + S(38.0f) + (rh + rg) * 8.0f - S(2.0f), S(130.0f), S(30.0f) }, "Diaqnostika", dt, c.in, false)) WriteDiag(c);
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

    bool Available(CVehicleGTA* v, int part);   // modelde bu hisse ayrica obyekt kimi varmi
    bool IsHidden (CVehicleGTA* v, int part);
    bool Toggle   (CVehicleGTA* v, int part);
    void ShowAll  (CVehicleGTA* v);
    void HideAll  (CVehicleGTA* v);
    void Apply    (CVehicleGTA* v);              // render hook-dan
    void Diag     (CVehicleGTA* v, void* file);  // FILE* -> hansi frame/atomlar tapildi
}
'''

VEHDETACH_CPP = r'''#include "VehDetach.h"
#include <cstdio>
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
        "Sol ön qanad", "Sağ ön qanad", "Ön farlar", "Stoplar",
        "Ön şüşə", "Ekzoz", "Əlavə detallar"
    };

    // GTA SA vehicle.dff frame adlari (her model hamisina malik deyil)
    static const char* const kFrames[P_COUNT][6] = {
        { "bonnet_dummy", "bonnet_ok", "bonnet" },
        { "boot_dummy", "boot_ok", "boot" },
        { "bump_front_dummy", "bump_front_ok", "bump_front" },
        { "bump_rear_dummy", "bump_rear_ok", "bump_rear" },
        { "door_lf_dummy", "door_lf_ok" },
        { "door_rf_dummy", "door_rf_ok" },
        { "door_lr_dummy", "door_lr_ok" },
        { "door_rr_dummy", "door_rr_ok" },
        { "wing_lf_dummy", "wing_lf_ok" },
        { "wing_rf_dummy", "wing_rf_ok" },
        { "headlights", "headlights2" },
        { "taillights", "taillights2" },
        { "windscreen_dummy", "windscreen_ok", "windscreen" },
        { "exhaust_ok", "exhaust_dummy", "exhaust" },
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

    static void Walk(RwFrame* f, std::vector<Saved>& out, int depth)
    {
        if (!f || depth > 8) return;
        RwFrameForAllObjects(f, CollectCB, &out);
        for (RwFrame* c = f->child; c; c = c->next) Walk(c, out, depth + 1);
    }

    static void Collect(CVehicleGTA* v, int part, std::vector<Saved>& out)
    {
        for (const char* nm : kFrames[part]) {
            if (!nm) break;
            RwFrame* fr = CClumpModelInfo::GetFrameFromName((RpClump*)v->m_pRwClump, nm);
            if (fr) Walk(fr, out, 0);
        }
    }

    static State* Get(CVehicleGTA* v, bool create)
    {
        if (!v || !v->m_pRwClump) return nullptr;
        auto it = g_st.find(v);
        if (it != g_st.end()) {
            State& s = it->second;
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
        if (s && (s->mask & (1u << part))) return true;
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
        if (s->mask & bit) {
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

    void ShowAll(CVehicleGTA* v) { for (int p = 0; p < P_COUNT; ++p) if (IsHidden(v, p)) Toggle(v, p); }
    void HideAll(CVehicleGTA* v) { for (int p = 0; p < P_COUNT; ++p) if (!IsHidden(v, p)) Toggle(v, p); }

    void Apply(CVehicleGTA* v)
    {
        if (g_st.empty()) return;
        State* s = Get(v, false);
        if (!s || !s->mask) return;
        for (int p = 0; p < P_COUNT; ++p)
            if (s->mask & (1u << p))
                for (auto& sv : s->saved[p]) sv.o->flags &= ~kRenderFlag;
    }

    void Diag(CVehicleGTA* v, void* file)
    {
        FILE* f = (FILE*)file;
        if (!f || !v) return;
        fprintf(f, "clump=%p\n", (void*)v->m_pRwClump);
        if (!v->m_pRwClump) return;
        for (int p = 0; p < P_COUNT; ++p) {
            fprintf(f, "[%s]", kLabel[p]);
            for (const char* nm : kFrames[p]) {
                if (!nm) break;
                RwFrame* fr = CClumpModelInfo::GetFrameFromName((RpClump*)v->m_pRwClump, nm);
                std::vector<Saved> tmp;
                if (fr) Walk(fr, tmp, 0);
                fprintf(f, " %s=%s/%d", nm, fr ? "frame" : "yox", (int)tmp.size());
            }
            fprintf(f, " hidden=%d\n", IsHidden(v, p) ? 1 : 0);
        }
    }
}
'''


def replace_all_optional(text, anchor, new):
    return ws_re(anchor).sub(lambda m: new, text)


def patch_gui(t):
    if TAG in t:
        raise PatchError("gui.cpp artiq patch olunub (%s)" % TAG)
    t = replace1(t, r'''static inline ImU32 Col(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, (int)((float)a * s_A)); }
        static inline ImU32 Accent(float /*istifade olunmur*/, int a = 255) { return Col(78, 142, 235, a); }''',
                 GUI_COL, "gui:Col/Accent")
    t = replace1(t, r'''Fill(d, primary ? Accent(0.0f, 200 + (int)(40.0f * p)) : Col(255, 255, 255, 20 + (int)(30.0f * p)), S(12.0f));
        TxtC(S(15.0f), d, Col(244, 247, 253), label, true);''', GUI_SMALLBTN, "gui:SmallBtn")
    t = replace_between(t, "static void Glass(const mm::Rc& P, float rad)",
                        "static void EmptyState(const mm::Rc& r, const char* title, const char* sub)", GUI_GLASS, "gui:Glass")
    t = replace_all_optional(t, "Fill(pvr, Col((int)s_hl[0], (int)s_hl[1], (int)s_hl[2], 255), S(12.0f));",
                             "Fill(pvr, ColT((int)s_hl[0], (int)s_hl[1], (int)s_hl[2], 255), S(12.0f));")
    t = replace_all_optional(t, "Dot(cxs, cys, S(20.0f) - S(2.0f) * p, Col(r, g, b, 255));",
                             "Dot(cxs, cys, S(20.0f) - S(2.0f) * p, ColT(r, g, b, 255));")
    t = insert_before(t, "void RenderHandlingDialog() {", MM3 + "\n", "gui:mm3")
    t = replace_between(t, "// segmentli idareetme (iOS)", "// ---- sehife mezmunu ----", GUI_TABS, "gui:tabs")
    t = replace1(t, "const float cx0 = P.x + S(24.0f) + (1.0f - pe) * S(26.0f) * s_tabDir;", GUI_CX0, "gui:cx0")
    t = replace1(t, "const float cw = P.w - S(48.0f);", GUI_CW, "gui:cw")
    t = replace_between(t, "else if (s_tab == 1) // HANDLING", "else if (s_tab == 2) // FAR", GUI_HANDLING, "gui:handling")
    t = replace1(t, "else if (s_tab == 2) // FAR", "else if (pg == 2)                                       // FAR RENGI", "gui:far")
    t = replace_between(t, "else if (s_tab == 3) // TUNING (GARAGE_V5)", "else if (s_tab == 4) // NOMRE (PLATEADJ_V7)", GUI_TUNING, "gui:tuning")
    t = replace1(t, "else if (s_tab == 4) // NOMRE (PLATEADJ_V7)", "else if (pg == 4)                                       // NOMRE", "gui:nomre")
    return t + "\n// " + TAG + "\n"


def patch_vehicle_cpp(t):
    if TAG in t:
        raise PatchError("vehicle.cpp artiq patch olunub")
    t = replace_between(t, "void CVehicle::AddComponent(int iComponentID)", "void CVehicle::SetPaintJob(uint8_t bytePaintJobID)", VEH_ADDCOMP, "veh:AddComponent")
    t = replace1(t, "static std::map<CVehicleGTA*, std::vector<uint8_t>> g_HndxOrig;", VEH_PHYS, "veh:phys")
    t = replace_between(t, "static void HndxSnap(CVehicle* v)", "static int HndxHexVal(char c)", VEH_SNAP, "veh:snap")
    t = replace_between(t, "bool HndExt_Get(CVehicle* v, const char* name, float* out)",
                        "// ---- slotlar 1..5 : SAMP/handling_slots.txt ----", VEH_API, "veh:api")
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
                      '#include "../VehDetach.h"   // MM_V8\n', "vgc:inc")
    t = replace1(t, "VehControls::ApplyLampArray(veh);",
                 "VehControls::ApplyLampArray(veh);\n    VehDetach::Apply(veh);   // MM_V8", "vgc:hook")
    return t + "\n// " + TAG + "\n"


def patch_cmake():
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
    plan, created = {}, {}
    try:
        gui = locate("samp/gui/gui.cpp")
        veh = locate("samp/game/vehicle.cpp")
        vgh = locate("samp/game/Entity/CVehicleGTA.h")
        vgc = locate("samp/game/Entity/CVehicleGTA.cpp")
        restored = []
        for p, fn in ((gui, patch_gui), (veh, patch_vehicle_cpp), (vgh, patch_vehgta_h), (vgc, patch_vehgta_cpp)):
            t, was = read_clean(p)
            if was:
                restored.append(p)
            plan[p] = fn(t)
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
        if not os.path.isfile(p + ".bak_v8"):
            shutil.copyfile(p, p + ".bak_v8")
        with open(p, "w", encoding="utf-8", newline="") as f:
            f.write(new)
        print("patch: %s" % os.path.relpath(p, ROOT))
    for p, new in created.items():
        with open(p, "w", encoding="utf-8", newline="") as f:
            f.write(new)
        print("yeni : %s" % os.path.relpath(p, ROOT))
    if restored:
        print("(v7 geri qaytarildi: %d fayl)" % len(restored))
    print("\nHazirdir. Qur.  Bir sey islemese: /mm -> Tuning -> 'Detallari sok' ya da Handling -> 'Diaqnostika' bas, SAMP/mm_diag.txt faylini gonder.")


if __name__ == "__main__":
    main()
