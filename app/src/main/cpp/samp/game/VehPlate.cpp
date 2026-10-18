#include "../main.h"
#include "vehicle.h"
#include "VehPlate.h"
#include "java/jniutil.h"

#include <dlfcn.h>
#include <jni.h>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

extern CJavaWrapper* pJavaWrapper;

// Pikseller duzgun reng gostermirse (qirmizi/mavi yerini deyisib): 1 et
#define PLATE_SWAP_RB 0

namespace
{
    // RenderWare sabitleri
    const int RW_RASTER_TEXTURE = 0x04;
    const int RW_FORMAT_8888    = 0x0500;
    const int RW_LOCK_WRITE     = 0x02;
    const int RW_FILTER_LINEAR  = 2;

    typedef void*      (*fnRasterCreate)(int w, int h, int depth, int flags);
    typedef uint8_t*   (*fnRasterLock)(void* raster, uint8_t level, int mode);
    typedef void*      (*fnRasterUnlock)(void* raster);
    typedef void*      (*fnRasterDestroy)(void* raster);
    typedef RwTexture* (*fnTextureCreate)(void* raster);
    typedef int        (*fnTextureDestroy)(RwTexture* tex);
    typedef RwTexture* (*fnTextureSetFilter)(RwTexture* tex, int mode);

    fnRasterCreate     pRasterCreate  = nullptr;
    fnRasterLock       pRasterLock    = nullptr;
    fnRasterUnlock     pRasterUnlock  = nullptr;
    fnRasterDestroy    pRasterDestroy = nullptr;
    fnTextureCreate    pTextureCreate = nullptr;
    fnTextureDestroy   pTextureDestroy = nullptr;
    fnTextureSetFilter pTextureFilter = nullptr;

    bool g_resolved = false;
    bool g_rwOk     = false;

    struct Pending { uint64_t key; int w; int h; std::vector<uint8_t> px; };

    std::mutex                               g_mtx;
    std::vector<Pending>                     g_pending;
    std::unordered_map<uint64_t, std::string> g_text;   // canli masin -> nomre metni
    std::unordered_map<uint64_t, RwTexture*>  g_tex;    // bizim yaratdigimiz teksturalar
    std::vector<RwTexture*>                  g_kill;
    std::vector<RwTexture*>                  g_killOld;

    template <typename T> T Sym(void* h, const char* name)
    {
        void* p = h ? dlsym(h, name) : nullptr;
        if (!p) p = dlsym(RTLD_DEFAULT, name);
        if (!p) FLog("[PLATE] symbol yoxdur: %s", name);
        return reinterpret_cast<T>(p);
    }

    bool ResolveRw()
    {
        if (g_resolved) return g_rwOk;
        g_resolved = true;

        void* h = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
        pRasterCreate   = Sym<fnRasterCreate>(h, "RwRasterCreate");
        pRasterLock     = Sym<fnRasterLock>(h, "RwRasterLock");
        pRasterUnlock   = Sym<fnRasterUnlock>(h, "RwRasterUnlock");
        pRasterDestroy  = Sym<fnRasterDestroy>(h, "RwRasterDestroy");
        pTextureCreate  = Sym<fnTextureCreate>(h, "RwTextureCreate");
        pTextureDestroy = Sym<fnTextureDestroy>(h, "RwTextureDestroy");
        pTextureFilter  = Sym<fnTextureSetFilter>(h, "RwTextureSetFilterMode");

        g_rwOk = pRasterCreate && pRasterLock && pRasterUnlock && pTextureCreate && pTextureDestroy;
        FLog("[PLATE] RenderWare funksiyalari: %s", g_rwOk ? "OK" : "TAPILMADI");
        return g_rwOk;
    }
}

// ============================================================================
//  Java -> native: cizilmis nomre pikselleri (RGBA 512x128)
// ============================================================================
extern "C" JNIEXPORT void JNICALL
Java_com_samp_mobile_game_SAMP_onPlateBitmap(JNIEnv* env, jobject, jlong key, jint w, jint h, jbyteArray arr)
{
    if (!arr || w <= 0 || h <= 0 || w > 1024 || h > 512) return;
    const jsize need = w * h * 4;
    if (env->GetArrayLength(arr) < need) return;

    Pending p;
    p.key = (uint64_t)key;
    p.w = w;
    p.h = h;
    p.px.resize((size_t)need);
    env->GetByteArrayRegion(arr, 0, need, reinterpret_cast<jbyte*>(p.px.data()));

    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_pending.size() > 64) g_pending.erase(g_pending.begin());
    g_pending.push_back(std::move(p));
}

namespace VehPlate
{
    void Apply(CVehicle* veh, const char* text)
    {
        if (!veh || !veh->m_pVehicle || !text) return;

        char buf[33];
        strncpy(buf, text, 32);
        buf[32] = '\0';
        if (buf[0] == '\0') return;

        strncpy(veh->m_szPlateText, buf, sizeof(veh->m_szPlateText) - 1);
        veh->m_szPlateText[sizeof(veh->m_szPlateText) - 1] = '\0';

        const uint64_t key = (uint64_t)(uintptr_t)veh->m_pVehicle;
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            auto it = g_text.find(key);
            if (it != g_text.end() && it->second == buf && g_tex.count(key)) return;   // artiq eynidir
            g_text[key] = buf;
        }

        if (pJavaWrapper) pJavaWrapper->RequestPlate(key, buf);
    }

    void OnVehicleDestroy(CVehicleGTA* veh)
    {
        if (!veh) return;
        const uint64_t key = (uint64_t)(uintptr_t)veh;

        std::lock_guard<std::mutex> lk(g_mtx);
        g_text.erase(key);
        auto it = g_tex.find(key);
        if (it != g_tex.end()) {
            if (veh->m_pCustomCarPlate == it->second) veh->m_pCustomCarPlate = nullptr;
            g_kill.push_back(it->second);
            g_tex.erase(it);
        }
    }

    void Process()
    {
        // evvelki kadrdan qalan kohne teksturalari indi sil (render artiq istifade etmir)
        std::vector<Pending> work;
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            if (g_pending.empty() && g_kill.empty() && g_killOld.empty()) return;
            work.swap(g_pending);
        }

        if (!ResolveRw()) {
            std::lock_guard<std::mutex> lk(g_mtx);
            g_kill.clear();
            g_killOld.clear();
            return;
        }

        {
            std::lock_guard<std::mutex> lk(g_mtx);
            for (RwTexture* t : g_killOld) if (t) pTextureDestroy(t);
            g_killOld.clear();
            g_killOld.swap(g_kill);
        }

        for (Pending& p : work)
        {
            // masin hele de var?
            {
                std::lock_guard<std::mutex> lk(g_mtx);
                if (!g_text.count(p.key)) continue;
            }

            void* raster = pRasterCreate(p.w, p.h, 32, RW_RASTER_TEXTURE | RW_FORMAT_8888);
            if (!raster) { FLog("[PLATE] RwRasterCreate ugursuz"); continue; }

            uint8_t* dst = pRasterLock(raster, 0, RW_LOCK_WRITE);
            if (!dst) {
                FLog("[PLATE] RwRasterLock ugursuz");
                if (pRasterDestroy) pRasterDestroy(raster);
                continue;
            }

            const size_t n = (size_t)p.w * p.h;
#if PLATE_SWAP_RB
            for (size_t i = 0; i < n; ++i) {
                dst[i * 4 + 0] = p.px[i * 4 + 2];
                dst[i * 4 + 1] = p.px[i * 4 + 1];
                dst[i * 4 + 2] = p.px[i * 4 + 0];
                dst[i * 4 + 3] = p.px[i * 4 + 3];
            }
#else
            memcpy(dst, p.px.data(), n * 4);
#endif
            pRasterUnlock(raster);

            RwTexture* tex = pTextureCreate(raster);
            if (!tex) {
                FLog("[PLATE] RwTextureCreate ugursuz");
                if (pRasterDestroy) pRasterDestroy(raster);
                continue;
            }
            if (pTextureFilter) pTextureFilter(tex, RW_FILTER_LINEAR);

            CVehicleGTA* gta = reinterpret_cast<CVehicleGTA*>((uintptr_t)p.key);

            std::lock_guard<std::mutex> lk(g_mtx);
            if (!g_text.count(p.key)) {          // arada masin silinibse
                g_kill.push_back(tex);
                continue;
            }
            auto it = g_tex.find(p.key);
            if (it != g_tex.end() && it->second) g_kill.push_back(it->second);
            g_tex[p.key] = tex;
            gta->m_pCustomCarPlate = tex;
        }
    }
}
