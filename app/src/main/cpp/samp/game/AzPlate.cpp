// Azerbaycan nomre lovhesi: MaterialTextGenerator ile tekstura cizilir, masinin render-i
// zamani plate materialinin teksturu muveqqeti evez olunur (material model uzre ORTAQDIR).
#include "../main.h"
#include "game.h"
#include "../gui/gui.h"
#include "RW/RenderWare.h"
#include "RW/rpworld.h"
#include "Scene.h"
#include "VisibilityPlugins.h"
#include "Entity/CVehicleGTA.h"
#include "game/Models/ModelInfo.h"
#include "../vendor/armhook/patch.h"
#include "AzPlate.h"

#include <cctype>
#include <algorithm>
#include <unistd.h>
#include <sys/uio.h>
#include <cstdio>
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

    // PLATE_MULTI_STRATEGY: 0 = deqiq adlar, 1 = ehtiyat adlar, 2 = en/hundurluk nisbeti (3:1 .. 8.5:1)
    int g_plateLevel = 0;

    bool SafeRead(uintptr_t addr, void* out, size_t n)
    {
        if (addr < 0x10000) return false;
        iovec l{ out, n };
        iovec r{ (void*)addr, n };
        return process_vm_readv(getpid(), &l, 1, &r, 1, 0) == (ssize_t)n;
    }

    void LowerCopy(const char* n, char* b, size_t cap)
    {
        size_t i = 0;
        if (n) for (; n[i] && i < cap - 1; ++i) b[i] = (char)tolower((unsigned char)n[i]);
        b[i] = 0;
    }

    bool IsPlateName(const char* n, int level)
    {
        if (!n || !n[0]) return false;
        char b[40];
        LowerCopy(n, b, sizeof(b));
        static const char* kStrict[] = { "nomer", "nomre", "carplate", "numberplate", nullptr };
        static const char* kLoose[]  = { "plate", "number", "licen", nullptr };
        const char** keys = (level == 0) ? kStrict : kLoose;
        for (int k = 0; keys[k]; ++k)
            if (strstr(b, keys[k])) return true;
        return false;
    }

    bool LooksLikePlateTexture(RwTexture* t)
    {
        if (!t || !t->raster) return false;
        const int w = (int)RwRasterGetWidth(t->raster);
        const int h = (int)RwRasterGetHeight(t->raster);
        if (w < 32 || h < 4) return false;
        const float ar = (float)w / (float)h;
        if (ar < 3.0f || ar > 8.5f) return false;

        char b[40];
        LowerCopy(t->name, b, sizeof(b));
        static const char* kBad[] = { "light", "lamp", "glass", "window", "chrome", "grill", "vent",
                                      "sticker", "decal", "wheel", "tire", nullptr };
        for (int k = 0; kBad[k]; ++k)
            if (strstr(b, kBad[k])) return false;
        return true;
    }


    RpMaterial* MatCB(RpMaterial* mat, void*)
    {
        if (!mat || !mat->texture || !g_collect) return mat;

        const bool ok = (g_plateLevel < 2) ? IsPlateName(mat->texture->name, g_plateLevel)
                                           : LooksLikePlateTexture(mat->texture);
        if (ok && std::find(g_collect->begin(), g_collect->end(), mat) == g_collect->end())
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

        // 0) oyunun oz lovhe materiali: modelin m_pPlateMaterial gostericisi (yaddasa tehlukesiz baxilir)
        {
            auto* mi = CModelInfo::GetVehicleModelInfo(veh->m_nModelIndex);
            if (mi)
            {
                RpMaterial* pm = mi->m_pPlateMaterial;
                const uintptr_t a = (uintptr_t)pm;
                RwTexture* tx = nullptr;
                uint32_t probe = 0;
                if (pm && (a & 3) == 0 &&
                    SafeRead(a, &tx, sizeof(tx)) && tx &&
                    SafeRead((uintptr_t)tx, &probe, sizeof(probe)))
                {
                    out.push_back(pm);
                    return;
                }
            }
        }

        // 1-3) clump icinde ad / nisbet ile axtaris
        for (g_plateLevel = 0; g_plateLevel <= 2 && out.empty(); ++g_plateLevel)
        {
            g_collect = &out;
            RpClumpForAllAtomics(veh->m_pRwClump, AtomCB, nullptr);
            g_collect = nullptr;
        }
        g_plateLevel = 0;
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


// ----------------------------------------------------------------------------
//  /dump : masinin atomic/material siyahisi + isiq dummy-leri
// ----------------------------------------------------------------------------
extern char* g_pszStorage;

namespace
{
    FILE* g_dumpF    = nullptr;
    int   g_dumpAtom = 0;
    int   g_dumpMat  = 0;

    RpMaterial* DumpMatCB(RpMaterial* mat, void*)
    {
        if (g_dumpF && mat) {
            const char* tn = mat->texture ? mat->texture->name : "(texture yox)";
            const char* mn = mat->texture ? mat->texture->mask : "";
            fprintf(g_dumpF, "  mat#%d tex=\"%s\" mask=\"%s\" rgba=%d,%d,%d,%d\n",
                    g_dumpMat++, tn, mn,
                    (int)mat->color.red, (int)mat->color.green, (int)mat->color.blue, (int)mat->color.alpha);
        }
        return mat;
    }

    RpAtomic* DumpAtomCB(RpAtomic* atomic, void*)
    {
        if (g_dumpF) fprintf(g_dumpF, "atomic#%d\n", g_dumpAtom++);
        auto geo = RpAtomicGetGeometry(atomic);
        if (geo) RpGeometryForAllMaterials(geo, DumpMatCB, nullptr);
        return atomic;
    }
}

namespace AzPlate
{
    bool Dump(CVehicleGTA* veh, std::string& outPath)
    {
        if (!veh || !veh->m_pRwClump || !g_pszStorage) return false;

        outPath = std::string(g_pszStorage) + "/vehdump.txt";
        FILE* f = fopen(outPath.c_str(), "a");
        if (!f) return false;

        g_dumpF = f;
        g_dumpAtom = 0;
        g_dumpMat = 0;

        fprintf(f, "\n===== model %d =====\n", (int)veh->m_nModelIndex);
        auto* mi = CModelInfo::GetVehicleModelInfo(veh->m_nModelIndex);
        if (mi && mi->m_pVehicleStruct) {
            for (int i = 0; i < 4; ++i) {
                const CVector& d = mi->m_pVehicleStruct->m_avDummyPos[i];
                fprintf(f, "dummy[%d] = %.3f %.3f %.3f\n", i, d.x, d.y, d.z);
            }
        }
        RpClumpForAllAtomics(veh->m_pRwClump, DumpAtomCB, nullptr);
        fprintf(f, "(atomic=%d, material=%d)\n", g_dumpAtom, g_dumpMat);

        fclose(f);
        g_dumpF = nullptr;
        return true;
    }
}

#include <cstdlib>
// ===== PLATE_V7: PNG nomre lovhesi (3D, PNG + plate.ttf) =====
#include <map>
#include <cmath>
#include <cstdint>
extern char* g_pszStorage;
extern RwImage*  (*RtPNGImageRead)(const RwChar* imageName);
extern RwUInt8*  (*RwRasterLock)(RwRaster* raster, RwUInt8 level, RwInt32 lockMode);
extern RwRaster* (*RwRasterUnlock)(RwRaster* raster);
extern RwBool    (*RwImageDestroy)(RwImage* image);

ImFont* g_pPlateFont = nullptr;   // imguiwrapper.cpp doldurur (assets/Fonts/plate.ttf)

namespace PlateImpl
{
    // ------------------------------------------------------------ mini JSON
    struct JV
    {
        int t = 0;   // 0 yox, 1 eded, 2 setir, 3 massiv, 4 obyekt, 5 bool
        double n = 0.0;
        std::string s;
        std::vector<JV> a;
        std::vector<std::pair<std::string, JV> > o;
    };

    static void Ws(const char*& p) { while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p; }

    static bool Parse(const char*& p, JV& v, int depth)
    {
        if (depth > 16) return false;
        Ws(p);
        if (*p == '{')
        {
            ++p; v.t = 4; Ws(p);
            if (*p == '}') { ++p; return true; }
            while (true)
            {
                Ws(p);
                if (*p != '"') return false;
                JV k;
                if (!Parse(p, k, depth + 1)) return false;
                Ws(p);
                if (*p != ':') return false;
                ++p;
                JV val;
                if (!Parse(p, val, depth + 1)) return false;
                v.o.push_back(std::make_pair(k.s, val));
                Ws(p);
                if (*p == ',') { ++p; continue; }
                if (*p == '}') { ++p; return true; }
                return false;
            }
        }
        if (*p == '[')
        {
            ++p; v.t = 3; Ws(p);
            if (*p == ']') { ++p; return true; }
            while (true)
            {
                JV e;
                if (!Parse(p, e, depth + 1)) return false;
                v.a.push_back(e);
                Ws(p);
                if (*p == ',') { ++p; continue; }
                if (*p == ']') { ++p; return true; }
                return false;
            }
        }
        if (*p == '"')
        {
            ++p; v.t = 2;
            while (*p && *p != '"')
            {
                if (*p == '\\' && p[1]) { ++p; v.s.push_back(*p == 'n' ? '\n' : (*p == 't' ? '\t' : *p)); }
                else v.s.push_back(*p);
                ++p;
            }
            if (*p != '"') return false;
            ++p;
            return true;
        }
        if (!strncmp(p, "true", 4))  { v.t = 5; v.n = 1.0; p += 4; return true; }
        if (!strncmp(p, "false", 5)) { v.t = 5; v.n = 0.0; p += 5; return true; }
        if (!strncmp(p, "null", 4))  { p += 4; return true; }
        char* end = nullptr;
        v.n = strtod(p, &end);
        if (end == p) return false;
        v.t = 1;
        p = end;
        return true;
    }

    static const JV* Find(const JV& o, const char* key)
    {
        for (size_t i = 0; i < o.o.size(); ++i) if (o.o[i].first == key) return &o.o[i].second;
        return nullptr;
    }

    // ------------------------------------------------------------ vezyet
    struct ModelCfg { bool hasF = false, hasR = false; PlateCfg f, r; std::string name; };

    static std::map<int, ModelCfg> g_models;
    static float g_textBox[4] = { 0.181f, 0.11f, 0.966f, 0.806f };   // plaka sekli daxilinde yazi sahesi (0..1)
    static bool  g_swapRB   = false;
    static bool  g_cfgLoaded = false;
    static float g_aspect   = 1235.0f / 273.0f;                      // plaka eni / hundurluyu
    static std::string g_err;

    struct Entry { uint32_t ct = 0; std::string text; bool front = true, rear = true; };
    static std::map<CVehicleGTA*, Entry> g_ent;
    static std::mutex g_mtx;

    static std::string P(const char* rel)
    {
        return std::string(g_pszStorage ? g_pszStorage : "") + "/SAMP/" + rel;
    }

    static void ReadVec(const JV* a, float* out, int n)
    {
        if (!a || a->t != 3) return;
        for (int i = 0; i < n && i < (int)a->a.size(); ++i) out[i] = (float)a->a[i].n;
    }

    static void ReadSide(const JV* o, PlateCfg& c)
    {
        if (!o || o->t != 4) return;
        ReadVec(Find(*o, "pos"), c.pos, 3);
        ReadVec(Find(*o, "rot"), c.rot, 3);
        const JV* w = Find(*o, "w");
        if (w && w->t == 1 && w->n > 0.05) c.w = (float)w->n;
    }

    static void EnsureCfg()
    {
        if (g_cfgLoaded) return;
        g_cfgLoaded = true;
        FILE* f = fopen(P("plates.json").c_str(), "rb");
        if (!f) return;
        std::string txt;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) txt.append(buf, n);
        fclose(f);

        JV root;
        const char* p = txt.c_str();
        if (!Parse(p, root, 0) || root.t != 4) { g_err = "plates.json oxunmadi (JSON xetasi)"; return; }

        ReadVec(Find(root, "text_box"), g_textBox, 4);
        const JV* sw = Find(root, "swap_rb");
        if (sw && sw->t == 5) g_swapRB = sw->n != 0.0;
        const JV* ms = Find(root, "models");
        if (ms && ms->t == 4)
        {
            for (size_t i = 0; i < ms->o.size(); ++i)
            {
                const int id = atoi(ms->o[i].first.c_str());
                const JV& m = ms->o[i].second;
                if (m.t != 4) continue;
                ModelCfg mc;
                const JV* nm = Find(m, "name");
                if (nm && nm->t == 2) mc.name = nm->s;
                const JV* fr = Find(m, "front");
                const JV* rr = Find(m, "rear");
                if (fr) { mc.f = PlateCfg(); mc.f.w = 0.52f; ReadSide(fr, mc.f); mc.hasF = true; }
                if (rr) { mc.r = PlateCfg(); mc.r.w = 0.52f; ReadSide(rr, mc.r); mc.hasR = true; }
                g_models[id] = mc;
            }
        }
    }

    static PlateCfg AutoCfg(CVehicleGTA* veh, bool front)
    {
        PlateCfg c;
        memset(&c, 0, sizeof(c));
        c.w = 0.52f;
        float fy = 2.0f, fz = -0.15f, ry = -2.1f, rz = -0.1f;
        auto* mi = CModelInfo::GetVehicleModelInfo(veh->m_nModelIndex);
        if (mi && mi->m_pVehicleStruct)
        {
            const CVector& d0 = mi->m_pVehicleStruct->m_avDummyPos[0];   // on isiqlar
            const CVector& d1 = mi->m_pVehicleStruct->m_avDummyPos[1];   // arxa isiqlar
            if (fabsf(d0.y) > 0.5f) { fy = d0.y + 0.03f; fz = d0.z - 0.30f; }
            if (fabsf(d1.y) > 0.5f) { ry = d1.y - 0.03f; rz = d1.z - 0.22f; }
        }
        if (front) { c.pos[1] = fy; c.pos[2] = fz; c.rot[2] = 180.0f; }
        else       { c.pos[1] = ry; c.pos[2] = rz; c.rot[2] = 0.0f; }
        return c;
    }

    // ------------------------------------------------------------ riyaziyyat
    struct V3 { float x, y, z; };

    static V3 Rot(V3 p, const float rot[3])
    {
        const float d2r = 0.0174532925f;
        const float rx = rot[0] * d2r, ry = rot[1] * d2r, rz = rot[2] * d2r;
        float c = cosf(ry), s = sinf(ry);
        V3 a = { p.x * c + p.z * s, p.y, -p.x * s + p.z * c };            // Y (yan egilme)
        c = cosf(rx); s = sinf(rx);
        V3 b = { a.x, a.y * c - a.z * s, a.y * s + a.z * c };             // X (on-arxa egilme)
        c = cosf(rz); s = sinf(rz);
        V3 d = { b.x * c - b.y * s, b.x * s + b.y * c, b.z };             // Z (donme)
        return d;
    }

    static V3 Dir(const RwMatrix& m, V3 l)
    {
        V3 r = { m.right.x * l.x + m.up.x * l.y + m.at.x * l.z,
                 m.right.y * l.x + m.up.y * l.y + m.at.y * l.z,
                 m.right.z * l.x + m.up.z * l.y + m.at.z * l.z };
        return r;
    }

    static V3 World(const RwMatrix& m, V3 l)
    {
        V3 d = Dir(m, l);
        V3 r = { m.pos.x + d.x, m.pos.y + d.y, m.pos.z + d.z };
        return r;
    }

    // ------------------------------------------------------------ tekstur
    static RwTexture* g_baseTex = nullptr;
    static bool g_baseTried = false;

    static RwTexture* BaseTex()
    {
        if (g_baseTex || g_baseTried) return g_baseTex;
        g_baseTried = true;
        EnsureCfg();

        if (!g_pszStorage)                    { g_err = "storage yolu yoxdur"; return nullptr; }
        if (!RtPNGImageRead || !RwRasterLock) { g_err = "RW funksiyalari yoxdur"; return nullptr; }

        const std::string path = P("plate/plate.png");
        RwImage* img = RtPNGImageRead(path.c_str());
        if (!img) { g_err = "plate.png oxunmadi: " + path; return nullptr; }
        if (img->depth != 32 || !img->cpPixels || img->width < 2 || img->height < 2)
        {
            g_err = "plate.png 32-bit (RGBA) deyil";
            if (RwImageDestroy) RwImageDestroy(img);
            return nullptr;
        }

        const int TW = 1024, TH = 256;
        RwRaster* ras = RwRasterCreate(TW, TH, 32, rwRASTERFORMAT8888 | rwRASTERTYPETEXTURE);
        if (!ras) { g_err = "raster yaradilmadi"; if (RwImageDestroy) RwImageDestroy(img); return nullptr; }
        uint8_t* px = RwRasterLock(ras, 0, rwRASTERLOCKWRITE);
        if (!px)  { g_err = "raster kilidlenmedi"; if (RwImageDestroy) RwImageDestroy(img); return nullptr; }

        const int stride = (ras->stride > 0) ? (int)ras->stride : TW * 4;
        const int iw = img->width, ih = img->height;
        const int istr = (img->stride > 0) ? img->stride : iw * 4;
        for (int y = 0; y < TH; ++y)
        {
            float fy = ((float)y + 0.5f) * (float)ih / (float)TH - 0.5f;
            if (fy < 0.0f) fy = 0.0f;
            int y0 = (int)fy; if (y0 > ih - 2) y0 = ih - 2;
            const float ty = fy - (float)y0;
            for (int x = 0; x < TW; ++x)
            {
                float fx = ((float)x + 0.5f) * (float)iw / (float)TW - 0.5f;
                if (fx < 0.0f) fx = 0.0f;
                int x0 = (int)fx; if (x0 > iw - 2) x0 = iw - 2;
                const float tx = fx - (float)x0;
                float ch[4];
                for (int k = 0; k < 4; ++k)
                {
                    const float a = img->cpPixels[y0 * istr + x0 * 4 + k];
                    const float b = img->cpPixels[y0 * istr + (x0 + 1) * 4 + k];
                    const float c = img->cpPixels[(y0 + 1) * istr + x0 * 4 + k];
                    const float d = img->cpPixels[(y0 + 1) * istr + (x0 + 1) * 4 + k];
                    ch[k] = (a * (1.0f - tx) + b * tx) * (1.0f - ty) + (c * (1.0f - tx) + d * tx) * ty;
                }
                uint8_t* o = px + y * stride + x * 4;
                o[0] = (uint8_t)(g_swapRB ? ch[2] : ch[0]);
                o[1] = (uint8_t)ch[1];
                o[2] = (uint8_t)(g_swapRB ? ch[0] : ch[2]);
                o[3] = (uint8_t)ch[3];
            }
        }
        RwRasterUnlock(ras);
        if (RwImageDestroy) RwImageDestroy(img);

        g_baseTex = RwTextureCreate(ras);
        if (!g_baseTex) g_err = "tekstur yaradilmadi";
        else g_err.clear();
        return g_baseTex;
    }

    // ImGui font atlasinin raster-i (yazi glifleri 3D-de bu teksturdan cizilir)
    static void* AtlasRaster()
    {
        ImFontAtlas* at = ImGui::GetIO().Fonts;
        void* id = (void*)at->TexID;
        if (!id) return nullptr;
        RwRaster* r = (RwRaster*)id;
        if ((int)RwRasterGetWidth(r) == at->TexWidth && (int)RwRasterGetHeight(r) == at->TexHeight) return r;
        RwTexture* t = (RwTexture*)id;
        if (t->raster && (int)RwRasterGetWidth(t->raster) == at->TexWidth && (int)RwRasterGetHeight(t->raster) == at->TexHeight) return t->raster;
        return r;
    }

    struct Vtx { float x, y, z, nx, ny, nz; uint32_t color; float u, v; };

    static void DrawTris(const Vtx* v, int nv, const uint16_t* idx, int ni, void* raster)
    {
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
        if (RwIm3DTransform((RwIm3DVertex*)v, (RwUInt32)nv, nullptr, 1 /* rwIM3D_VERTEXUV */))
        {
            RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST, (RwImVertexIndex*)idx, ni);
            RwIm3DEnd();
        }
    }

    static void DrawSide(CVehicleGTA* veh, const RwMatrix& m, bool front, const std::string& text, RwTexture* base, ImFont* font, void* atlas)
    {
        const PlateCfg c = Plate::GetCfg(veh, front);
        const float W = c.w, H = c.w / g_aspect;

        auto toWorld = [&](float x, float y, float z) -> V3 {
            V3 q = { x, y, z };
            V3 r = Rot(q, c.rot);
            V3 l = { r.x + c.pos[0], r.y + c.pos[1], r.z + c.pos[2] };
            return World(m, l);
        };

        // 1) plaka sekli
        {
            Vtx v[4];
            const float cx[4] = { -W * 0.5f,  W * 0.5f, W * 0.5f, -W * 0.5f };
            const float cz[4] = {  H * 0.5f,  H * 0.5f, -H * 0.5f, -H * 0.5f };
            const float uu[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
            const float vv[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
            for (int i = 0; i < 4; ++i)
            {
                const V3 w = toWorld(cx[i], 0.0f, cz[i]);
                Vtx t = { w.x, w.y, w.z, 0, 0, 0, 0xFFFFFFFFu, uu[i], vv[i] };
                v[i] = t;
            }
            static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
            DrawTris(v, 4, idx, 6, base->raster);
        }

        // 2) yazi (font atlasindan glif kvadlari)
        if (!atlas || !font || text.empty()) return;
        const ImFontGlyph* g8 = font->FindGlyph((ImWchar)'8');
        if (!g8) return;

        float totalAdv = 0.0f;
        for (size_t i = 0; i < text.size(); ++i)
        {
            const ImFontGlyph* g = font->FindGlyph((ImWchar)(unsigned char)text[i]);
            if (g) totalAdv += g->AdvanceX;
        }
        const float capTop = g8->Y0, capBot = g8->Y1;
        const float capH = capBot - capTop;
        if (totalAdv < 1.0f || capH < 1.0f) return;

        const float boxW = (g_textBox[2] - g_textBox[0]) * W;
        const float boxH = (g_textBox[3] - g_textBox[1]) * H;
        float mpp = (boxW * 0.94f) / totalAdv;               // metr / font pikseli
        const float mh = (boxH * 0.88f) / capH;
        if (mh < mpp) mpp = mh;
        const float ccx = ((g_textBox[0] + g_textBox[2]) * 0.5f - 0.5f) * W;
        const float ccz = (0.5f - (g_textBox[1] + g_textBox[3]) * 0.5f) * H;
        const float capMid = (capTop + capBot) * 0.5f;

        Vtx tv[4 * 16];
        uint16_t ti[6 * 16];
        int nv = 0, ni = 0;
        float pen = -totalAdv * 0.5f;
        for (size_t i = 0; i < text.size() && nv + 4 <= 4 * 16; ++i)
        {
            const ImFontGlyph* g = font->FindGlyph((ImWchar)(unsigned char)text[i]);
            if (!g) continue;
            if (text[i] != ' ')
            {
                const float x0 = ccx + (pen + g->X0) * mpp, x1 = ccx + (pen + g->X1) * mpp;
                const float z0 = ccz - (g->Y0 - capMid) * mpp, z1 = ccz - (g->Y1 - capMid) * mpp;
                const float gx[4] = { x0, x1, x1, x0 };
                const float gz[4] = { z0, z0, z1, z1 };
                const float gu[4] = { g->U0, g->U1, g->U1, g->U0 };
                const float gv[4] = { g->V0, g->V0, g->V1, g->V1 };
                for (int k = 0; k < 4; ++k)
                {
                    const V3 w = toWorld(gx[k], -0.003f, gz[k]);      // plakanin ustunde (z-fight olmasin)
                    Vtx t = { w.x, w.y, w.z, 0, 0, 0, 0xFF0F0F0Fu, gu[k], gv[k] };
                    tv[nv + k] = t;
                }
                ti[ni++] = (uint16_t)nv;       ti[ni++] = (uint16_t)(nv + 1); ti[ni++] = (uint16_t)(nv + 2);
                ti[ni++] = (uint16_t)nv;       ti[ni++] = (uint16_t)(nv + 2); ti[ni++] = (uint16_t)(nv + 3);
                nv += 4;
            }
            pen += g->AdvanceX;
        }
        if (nv > 0) DrawTris(tv, nv, ti, ni, atlas);
    }
}

namespace Plate
{
    bool Normalize(const std::string& in, std::string& out)
    {
        std::string n;
        if (!AzPlate::Normalize(in, n)) return false;     // "10-AA-123"
        out = n.substr(0, 2) + " " + n.substr(3, 2) + " " + n.substr(6, 3);
        return true;
    }

    bool Set(CVehicleGTA* veh, const std::string& text)
    {
        if (!veh) return false;
        std::lock_guard<std::mutex> lk(PlateImpl::g_mtx);
        PlateImpl::Entry& e = PlateImpl::g_ent[veh];
        if (e.ct != (uint32_t)veh->m_nCreationTime) { e = PlateImpl::Entry(); e.ct = (uint32_t)veh->m_nCreationTime; }
        e.text = text;
        return true;
    }

    void Remove(CVehicleGTA* veh)
    {
        std::lock_guard<std::mutex> lk(PlateImpl::g_mtx);
        PlateImpl::g_ent.erase(veh);
    }

    bool Get(CVehicleGTA* veh, std::string& text)
    {
        if (!veh) return false;
        std::lock_guard<std::mutex> lk(PlateImpl::g_mtx);
        std::map<CVehicleGTA*, PlateImpl::Entry>::iterator it = PlateImpl::g_ent.find(veh);
        if (it == PlateImpl::g_ent.end() || it->second.ct != (uint32_t)veh->m_nCreationTime) return false;
        text = it->second.text;
        return true;
    }

    void SetSides(CVehicleGTA* veh, bool front, bool rear)
    {
        if (!veh) return;
        std::lock_guard<std::mutex> lk(PlateImpl::g_mtx);
        std::map<CVehicleGTA*, PlateImpl::Entry>::iterator it = PlateImpl::g_ent.find(veh);
        if (it == PlateImpl::g_ent.end() || it->second.ct != (uint32_t)veh->m_nCreationTime) return;
        it->second.front = front;
        it->second.rear = rear;
    }

    void GetSides(CVehicleGTA* veh, bool& front, bool& rear)
    {
        front = rear = true;
        if (!veh) return;
        std::lock_guard<std::mutex> lk(PlateImpl::g_mtx);
        std::map<CVehicleGTA*, PlateImpl::Entry>::iterator it = PlateImpl::g_ent.find(veh);
        if (it == PlateImpl::g_ent.end() || it->second.ct != (uint32_t)veh->m_nCreationTime) return;
        front = it->second.front;
        rear = it->second.rear;
    }

    PlateCfg GetCfg(CVehicleGTA* veh, bool front)
    {
        PlateImpl::EnsureCfg();
        std::map<int, PlateImpl::ModelCfg>::iterator it = PlateImpl::g_models.find((int)veh->m_nModelIndex);
        if (it != PlateImpl::g_models.end())
        {
            if (front && it->second.hasF) return it->second.f;
            if (!front && it->second.hasR) return it->second.r;
        }
        return PlateImpl::AutoCfg(veh, front);
    }

    void SetCfg(CVehicleGTA* veh, bool front, const PlateCfg& c)
    {
        PlateImpl::EnsureCfg();
        PlateImpl::ModelCfg& m = PlateImpl::g_models[(int)veh->m_nModelIndex];
        if (front) { m.f = c; m.hasF = true; }
        else       { m.r = c; m.hasR = true; }
    }

    void ResetCfg(CVehicleGTA* veh, bool front)
    {
        PlateImpl::EnsureCfg();
        std::map<int, PlateImpl::ModelCfg>::iterator it = PlateImpl::g_models.find((int)veh->m_nModelIndex);
        if (it == PlateImpl::g_models.end()) return;
        if (front) it->second.hasF = false; else it->second.hasR = false;
    }

    bool HasCustom(CVehicleGTA* veh, bool front)
    {
        PlateImpl::EnsureCfg();
        std::map<int, PlateImpl::ModelCfg>::iterator it = PlateImpl::g_models.find((int)veh->m_nModelIndex);
        if (it == PlateImpl::g_models.end()) return false;
        return front ? it->second.hasF : it->second.hasR;
    }

    bool SaveCfg()
    {
        PlateImpl::EnsureCfg();
        FILE* f = fopen(PlateImpl::P("plates.json").c_str(), "wb");
        if (!f) { PlateImpl::g_err = "plates.json yazilmadi"; return false; }
        fprintf(f, "{\n  \"_readme\": \"pos = masin merkezine nezeren metr (x sag, y irelli, z yuxari). rot = derece (x, y, z). rot=[0,0,0] plaka arxaya baxir; qabaq ucun z=180. w = plaka eni (metr).\",\n");
        fprintf(f, "  \"plate_image\": \"plate.png\",\n  \"swap_rb\": %s,\n", PlateImpl::g_swapRB ? "true" : "false");
        fprintf(f, "  \"text_box\": [%.3f, %.3f, %.3f, %.3f],\n  \"models\": {\n",
                PlateImpl::g_textBox[0], PlateImpl::g_textBox[1], PlateImpl::g_textBox[2], PlateImpl::g_textBox[3]);
        bool first = true;
        for (std::map<int, PlateImpl::ModelCfg>::iterator it = PlateImpl::g_models.begin(); it != PlateImpl::g_models.end(); ++it)
        {
            const PlateImpl::ModelCfg& m = it->second;
            if (!m.hasF && !m.hasR) continue;
            fprintf(f, "%s    \"%d\": {", first ? "" : ",\n", it->first);
            first = false;
            bool sep = false;
            if (!m.name.empty()) { fprintf(f, " \"name\": \"%s\"", m.name.c_str()); sep = true; }
            const PlateCfg* cs[2] = { &m.f, &m.r };
            const bool has[2] = { m.hasF, m.hasR };
            const char* nm[2] = { "front", "rear" };
            for (int k = 0; k < 2; ++k)
            {
                if (!has[k]) continue;
                fprintf(f, "%s\n      \"%s\": { \"pos\": [%.4f, %.4f, %.4f], \"rot\": [%.2f, %.2f, %.2f], \"w\": %.4f }",
                        sep ? "," : "", nm[k], cs[k]->pos[0], cs[k]->pos[1], cs[k]->pos[2],
                        cs[k]->rot[0], cs[k]->rot[1], cs[k]->rot[2], cs[k]->w);
                sep = true;
            }
            fprintf(f, "\n    }");
        }
        fprintf(f, "\n  }\n}\n");
        fclose(f);
        return true;
    }

    bool WorldPose(CVehicleGTA* veh, bool front, float pos[3], float n[3], float up[3])
    {
        if (!veh) return false;
        const PlateCfg c = GetCfg(veh, front);
        RwMatrix m;
        veh->GetMatrix(&m);
        PlateImpl::V3 lp = { c.pos[0], c.pos[1], c.pos[2] };
        const PlateImpl::V3 wp = PlateImpl::World(m, lp);
        const PlateImpl::V3 nl = { 0.0f, -1.0f, 0.0f }, ul = { 0.0f, 0.0f, 1.0f };
        const PlateImpl::V3 nw = PlateImpl::Dir(m, PlateImpl::Rot(nl, c.rot));
        const PlateImpl::V3 uw = PlateImpl::Dir(m, PlateImpl::Rot(ul, c.rot));
        pos[0] = wp.x; pos[1] = wp.y; pos[2] = wp.z;
        n[0] = nw.x;   n[1] = nw.y;   n[2] = nw.z;
        up[0] = uw.x;  up[1] = uw.y;  up[2] = uw.z;
        return true;
    }

    const char* LastError() { return PlateImpl::g_err.c_str(); }

    void* BaseRaster()
    {
        RwTexture* t = PlateImpl::BaseTex();
        return t ? (void*)t->raster : nullptr;
    }

    void Render(CVehicleGTA* veh)
    {
        if (!veh) return;
        PlateImpl::Entry e;
        {
            std::lock_guard<std::mutex> lk(PlateImpl::g_mtx);
            if (PlateImpl::g_ent.empty()) return;
            std::map<CVehicleGTA*, PlateImpl::Entry>::iterator it = PlateImpl::g_ent.find(veh);
            if (it == PlateImpl::g_ent.end()) return;
            if (it->second.ct != (uint32_t)veh->m_nCreationTime) { PlateImpl::g_ent.erase(it); return; }
            e = it->second;
        }
        if (!e.front && !e.rear) return;

        RwTexture* base = PlateImpl::BaseTex();
        if (!base || !base->raster) return;

        ImFont* font = g_pPlateFont ? g_pPlateFont : ImGui::GetFont();
        void* atlas = PlateImpl::AtlasRaster();

        RwMatrix m;
        veh->GetMatrix(&m);

        RwRenderStateSet(rwRENDERSTATETEXTUREFILTER,     RWRSTATE(rwFILTERLINEAR));
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE,       RWRSTATE(TRUE));
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,      RWRSTATE(TRUE));
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, RWRSTATE(TRUE));
        RwRenderStateSet(rwRENDERSTATESRCBLEND,          RWRSTATE(rwBLENDSRCALPHA));
        RwRenderStateSet(rwRENDERSTATEDESTBLEND,         RWRSTATE(rwBLENDINVSRCALPHA));
        RwRenderStateSet(rwRENDERSTATECULLMODE,          RWRSTATE(rwCULLMODECULLNONE));

        if (e.front) PlateImpl::DrawSide(veh, m, true,  e.text, base, font, atlas);
        if (e.rear)  PlateImpl::DrawSide(veh, m, false, e.text, base, font, atlas);

        RwRenderStateSet(rwRENDERSTATECULLMODE,          RWRSTATE(rwCULLMODECULLBACK));
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER,     RWRSTATE(NULL));
    }
}

