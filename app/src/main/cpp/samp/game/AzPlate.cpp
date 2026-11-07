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
