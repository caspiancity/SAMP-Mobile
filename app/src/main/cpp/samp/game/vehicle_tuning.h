#pragma once
//
// game/vehicle_tuning.h  (YENI FAYL - game/ qovluğuna at)
//
// Client-side, masin-basina handling + far rengi.
//
//  * Handling: oyun eyni handling ID-li BUTUN masinlar ucun TEK tHandlingData saxlayir.
//    Biz tenzimlenen masina oz ozel surətini veririk (m_pHandlingData bu suretə baxir),
//    qalan masinlara toxunulmur. Deyerler orijinalin FAIZI kimi redakte olunur
//    (100% = orijinal), buna gore vahid cevirmelerini texmin etmek lazim deyil.
//  * Far rengi: burada saxlanir, Coronas.cpp-deki RegisterCorona hook-u tetbiq edir.
//

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "game/Entity/CVehicleGTA.h"

namespace VehicleTuning
{
    // ------------------------------------------------------------------
    // tHandlingData qurulusu (PC SA ile eynidir). cTransmission 0x2C-de baslayir.
    // Bu static_assert kecmirse - ofsetleri yoxla (tHandlingData.h).
    // ------------------------------------------------------------------
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif
    constexpr size_t kOffTrans = offsetof(tHandlingData, m_transmissionData);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    static_assert(kOffTrans == 0x2C, "tHandlingData layout PC SA ile eyni deyil - ofsetleri yoxla");
    static_assert(sizeof(cTransmission) == 0x68, "cTransmission olcusu yanlisdir");

    constexpr size_t kOffMass  = 0x04;                          // float m_fMass
    constexpr size_t kOffDrag  = 0x10;                          // float m_fDragMult
    constexpr size_t kOffBrake = kOffTrans + sizeof(cTransmission); // 0x94 float m_fBrakeDeceleration
    constexpr size_t kOffSteer = kOffBrake + 0x0C;              // 0xA0 float m_fSteeringLock

    // ------------------------------------------------------------------
    struct Params
    {
        // faiz (100 = orijinal)
        float speed = 100.0f;
        float accel = 100.0f;
        float brake = 100.0f;
        float steer = 100.0f;
        float mass  = 100.0f;

        // far rengi
        bool    hl = false;
        uint8_t r = 255, g = 255, b = 255;

        bool IsDefaultHandling() const
        {
            auto eq = [](float v) { return v > 99.99f && v < 100.01f; };
            return eq(speed) && eq(accel) && eq(brake) && eq(steer) && eq(mass);
        }
    };

    struct Entry
    {
        uint32_t model = 0;   // masinin eyniliyini yoxlamaq ucun (pointer yeniden istifade oluna biler)
        uint32_t stamp = 0;

        tHandlingData* shared = nullptr;  // oyunun orijinal (paylasilan) handling-i
        tHandlingData* custom = nullptr;  // bizim ozel surət (malloc)
        tHandlingData  base{};            // orijinal deyerlerin snapshot-i
        float          baseMass = 0.0f;   // CPhysical::m_fMass orijinali

        Params p;
    };

    inline std::mutex                                g_mtx;
    inline std::unordered_map<CVehicleGTA*, Entry>   g_map;

    inline float Cl(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    // ---------------- daxili (g_mtx kilidi altinda) ----------------

    inline void FreeCustomLocked(CVehicleGTA* v, Entry& e, bool vehicleAlive)
    {
        if (!e.custom) return;

        if (vehicleAlive && v && v->m_pHandlingData == e.custom)
        {
            v->m_pHandlingData = e.shared;
            if (e.baseMass > 0.0f) v->m_fMass = e.baseMass;
        }
        std::free(e.custom);
        e.custom = nullptr;
    }

    inline Entry* FindLocked(CVehicleGTA* v)
    {
        if (!v || g_map.empty()) return nullptr;

        auto it = g_map.find(v);
        if (it == g_map.end()) return nullptr;

        Entry& e = it->second;

        const bool same =
            e.model == (uint32_t)v->m_nModelIndex &&
            e.stamp == (uint32_t)v->m_nCreationTime &&
            (!e.custom || v->m_pHandlingData == e.custom);

        if (!same)
        {
            // pointer basqa masina verilib ve ya oyun handling-i deyisib - kohne qeydi at
            FreeCustomLocked(v, e, false);
            g_map.erase(it);
            return nullptr;
        }
        return &e;
    }

    inline bool MakeCopyLocked(CVehicleGTA* v, Entry& e)
    {
        if (!v->m_pHandlingData) return false;

        void* mem = std::malloc(sizeof(tHandlingData));
        if (!mem) return false;

        e.shared = v->m_pHandlingData;
        std::memcpy(mem, e.shared, sizeof(tHandlingData));
        std::memcpy(reinterpret_cast<void*>(&e.base), e.shared, sizeof(tHandlingData));

        e.custom   = static_cast<tHandlingData*>(mem);
        e.baseMass = v->m_fMass;

        v->m_pHandlingData = e.custom;
        return true;
    }

    inline void ApplyHandlingLocked(CVehicleGTA* v, Entry& e)
    {
        uint8_t*       dst = reinterpret_cast<uint8_t*>(e.custom);
        const uint8_t* src = reinterpret_cast<const uint8_t*>(&e.base);

        auto F = [&](size_t off) -> float& { return *reinterpret_cast<float*>(dst + off); };
        auto B = [&](size_t off) -> float  { return *reinterpret_cast<const float*>(src + off); };

        const float ks  = Cl(e.p.speed, 25.0f, 300.0f) / 100.0f;
        const float ka  = Cl(e.p.accel, 25.0f, 500.0f) / 100.0f;
        const float kb  = Cl(e.p.brake, 25.0f, 400.0f) / 100.0f;
        const float kst = Cl(e.p.steer, 50.0f, 150.0f) / 100.0f;
        const float km  = Cl(e.p.mass,  25.0f, 400.0f) / 100.0f;

        cTransmission&       tr = e.custom->m_transmissionData;
        const cTransmission& bt = e.base.m_transmissionData;

        // tecil
        tr.m_fEngineAcceleration = bt.m_fEngineAcceleration * ka;

        // maks suret: dislilerin sürətlerini miqyasla + sürtünməni tərs miqyasla
        tr.m_fMaxGearVelocity       = bt.m_fMaxGearVelocity * ks;
        tr.m_fMaxVelocity           = bt.m_fMaxVelocity * ks;
        tr.m_maxReverseGearVelocity = bt.m_maxReverseGearVelocity * ks;

        float*       g  = reinterpret_cast<float*>(tr.m_aGears);
        const float* bg = reinterpret_cast<const float*>(bt.m_aGears);
        for (size_t i = 0; i < sizeof(tr.m_aGears) / sizeof(float); ++i)
            g[i] = bg[i] * ks;

        F(kOffDrag) = B(kOffDrag) / ks;

        // tormoz, sukan, kutle
        F(kOffBrake) = B(kOffBrake) * kb;
        F(kOffSteer) = B(kOffSteer) * kst;
        F(kOffMass)  = B(kOffMass)  * km;

        // masinin ozunun kutlesi spawn zamani handling-den kopyalanir - ayrica yazmaq lazimdir
        if (e.baseMass > 0.0f) v->m_fMass = e.baseMass * km;
    }

    // ---------------- ictimai API ----------------

    // Masinin cari parametrleri (qeyd yoxdursa standart: 100%, far standart)
    inline Params Get(CVehicleGTA* v)
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        Entry* e = FindLocked(v);
        return e ? e->p : Params{};
    }

    // Parametrleri tetbiq et (lazim olsa ozel handling suretini yaradir)
    inline void Set(CVehicleGTA* v, const Params& in)
    {
        if (!v) return;

        std::lock_guard<std::mutex> lk(g_mtx);

        Entry* e = FindLocked(v);
        if (!e)
        {
            if (in.IsDefaultHandling() && !in.hl) return;   // tetbiq etmeli bir sey yoxdur

            Entry& n = g_map[v];
            n.model = (uint32_t)v->m_nModelIndex;
            n.stamp = (uint32_t)v->m_nCreationTime;
            e = &n;
        }

        e->p = in;

        if (!e->custom && !in.IsDefaultHandling())
            MakeCopyLocked(v, *e);

        if (e->custom)
            ApplyHandlingLocked(v, *e);

        if (!e->custom && !e->p.hl)
            g_map.erase(v);   // izlenecek bir sey qalmadi
    }

    // Hamisini orijinala qaytar (handling + far)
    inline void Reset(CVehicleGTA* v)
    {
        if (!v) return;

        std::lock_guard<std::mutex> lk(g_mtx);
        Entry* e = FindLocked(v);
        if (!e) return;

        FreeCustomLocked(v, *e, true);
        g_map.erase(v);
    }

    // Corona hook-u ucun: bu masinin far rengi override olunubsa true
    inline bool GetHeadlight(CVehicleGTA* v, uint8_t& r, uint8_t& g, uint8_t& b)
    {
        if (!v) return false;

        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_map.empty()) return false;

        Entry* e = FindLocked(v);
        if (!e || !e->p.hl) return false;

        r = e->p.r; g = e->p.g; b = e->p.b;
        return true;
    }
}
