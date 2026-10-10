// ============================================================================
//  vehicle_tuning.cpp  (YENI FAYL -> game/ qovluguna qoy, CMake-e elave et)
//
//  Niye kohne yanasma islemirdi:
//   1) m_pHandlingData MODELE gore PAYLASILIR. Ora yazsan, eyni modelin
//      butun masinlari (baska oyunculardaki de) deyisir. Burada hemin
//      strukturun FERDI KOPYASI yaradilir ve yalniz bu masina baglanir.
//   2) CPhysical::m_fMass masin yaranarkən handling-den bir defe kopyalanir.
//      Yalniz handling->m_fMass-i deyismek fizikaya tesir etmir.
//   3) Suret ucun yalniz m_fMaxVelocity kifayet deyil: viteslerin kecid
//      cedveli (m_aGears) da miqyaslanmalidir.
//   4) Sliderler indi "faiz"dir (100% = standart), buna gore oyunun daxili
//      vahidlerini (fayl vahidi vs oyun vahidi) bilmeye ehtiyac yoxdur.
// ============================================================================
#include "vehicle_tuning.h"
#include "Entity/CVehicleGTA.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

// ---------------------------------------------------------------------------
// tHandlingData.h-da sahe adlari ferqlidirse YALNIZ bu 3 setri deyis.
// (cTransmission sahələri cTransmission.h-dan goturulub, onlar deqiqdir.)
// ---------------------------------------------------------------------------
#define HND_MASS(h)   ((h)->m_fMass)
#define HND_BRAKE(h)  ((h)->m_fBrakeDeceleration)
#define HND_STEER(h)  ((h)->m_fSteeringLock)

namespace
{
    // 0 = standart (override yoxdur, yalniz UI-da boz numune kimi gorunur)
    constexpr uint8_t kPalette[HEADLIGHT_COLOR_COUNT][3] = {
        { 90,  90,  90},   //  0 standart
        {255,  30,  30},   //  1 qirmizi
        {255, 120,   0},   //  2 narinci
        {255, 220,   0},   //  3 sari
        {160, 255,   0},   //  4 lime
        {  0, 255,  70},   //  5 yasil
        {  0, 255, 180},   //  6 firuze-yasil
        {  0, 230, 255},   //  7 cyan
        {  0, 140, 255},   //  8 goy-mavi
        { 40,  60, 255},   //  9 mavi
        {130,  60, 255},   // 10 binovseyi
        {200,   0, 255},   // 11 bənovseyi-qirmizi
        {255,  60, 200},   // 12 çəhrayi
        {255,   0, 100},   // 13 magenta
        {170, 210, 255},   // 14 buz agi
        {255, 200, 120},   // 15 isti ag
    };

    struct Entry
    {
        uint32_t       creationTime = 0;      // eyni adresde BASQA masini tanimaq ucun
        tHandlingData* orig         = nullptr; // oyunun umumi handling-i (HECVAXT deyismirik)
        tHandlingData* copy         = nullptr; // bu masina aid ferdi kopya
        float          baseMass     = 0.0f;    // CPhysical::m_fMass-in ilkin deyeri
        VehicleTuning  tune;
    };

    std::unordered_map<CVehicleGTA*, Entry> g_entries;
    std::mutex                              g_mutex;

    inline float Clamp(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

    inline bool IsNeutral(const VehicleTuning& t)
    {
        return std::fabs(t.speedPct - 100.0f) < 0.5f && std::fabs(t.accelPct - 100.0f) < 0.5f &&
               std::fabs(t.brakePct - 100.0f) < 0.5f && std::fabs(t.steerPct - 100.0f) < 0.5f &&
               std::fabs(t.massPct  - 100.0f) < 0.5f;
    }

    // g_mutex artiq tutulub olmalidir.
    Entry* Find(CVehicleGTA* veh)
    {
        auto it = g_entries.find(veh);
        if (it == g_entries.end()) return nullptr;

        if (it->second.creationTime != veh->m_nCreationTime)
        {
            // Bu adresde artiq basqa masin var (kohne masin silinib).
            // Yeni masin oyunun oz handling-i ile yaranib, bizim kopyaya baxmir.
            delete it->second.copy;
            g_entries.erase(it);
            return nullptr;
        }
        return &it->second;
    }

    Entry& FindOrCreate(CVehicleGTA* veh)
    {
        if (Entry* e = Find(veh)) return *e;
        Entry e;
        e.creationTime = veh->m_nCreationTime;
        return g_entries.emplace(veh, e).first->second;
    }

    void RestoreHandling(CVehicleGTA* veh, Entry& e)
    {
        if (!e.copy) return;
        if (veh->m_pHandlingData == e.copy) veh->m_pHandlingData = e.orig;   // ƏVVƏL pointeri qaytar
        veh->m_fMass = e.baseMass;
        delete e.copy;                                                       // SONRA sil
        e.copy = nullptr;
    }

    void ApplyHandling(CVehicleGTA* veh, Entry& e)
    {
        VehicleTuning& t = e.tune;
        t.speedPct = Clamp(t.speedPct,  50.0f, 250.0f);
        t.accelPct = Clamp(t.accelPct,  30.0f, 300.0f);
        t.brakePct = Clamp(t.brakePct,  30.0f, 300.0f);
        t.steerPct = Clamp(t.steerPct,  50.0f, 150.0f);
        t.massPct  = Clamp(t.massPct,   30.0f, 300.0f);
        if (t.headlight >= HEADLIGHT_COLOR_COUNT) t.headlight = 0;

        if (IsNeutral(t))
        {
            RestoreHandling(veh, e);
            return;
        }

        if (!e.copy)
        {
            if (!veh->m_pHandlingData) return;
            e.orig     = veh->m_pHandlingData;
            e.baseMass = veh->m_fMass;
            e.copy     = new tHandlingData(*e.orig);
        }
        veh->m_pHandlingData = e.copy;      // ferdi kopyaya bagla

        const float kSpd = t.speedPct * 0.01f;
        const float kAcc = t.accelPct * 0.01f;
        const float kBrk = t.brakePct * 0.01f;
        const float kStr = t.steerPct * 0.01f;
        const float kMas = t.massPct  * 0.01f;

        // Her zaman ORIGINAL-dan hesablayiriq -> tekrar tetbiq edende yigilmir (idempotent).
        const cTransmission& to = e.orig->m_transmissionData;
        cTransmission&       tc = e.copy->m_transmissionData;

        tc.m_fMaxVelocity           = to.m_fMaxVelocity        * kSpd;
        tc.m_fMaxGearVelocity       = to.m_fMaxGearVelocity    * kSpd;
        tc.m_maxReverseGearVelocity = to.m_maxReverseGearVelocity;
        tc.m_fEngineAcceleration    = to.m_fEngineAcceleration * kAcc;

        // Vites cedveli: her tTransmissionGear-in butun sahələri suretdir -> hamisini miqyasla.
        constexpr size_t kFloatsPerGear = sizeof(tTransmissionGear) / sizeof(float);
        for (int i = 0; i < 6; ++i)
        {
            const float* src = reinterpret_cast<const float*>(&to.m_aGears[i]);
            float*       dst = reinterpret_cast<float*>(&tc.m_aGears[i]);
            for (size_t j = 0; j < kFloatsPerGear; ++j) dst[j] = src[j] * kSpd;
        }

        HND_BRAKE(e.copy) = HND_BRAKE(e.orig) * kBrk;
        HND_STEER(e.copy) = HND_STEER(e.orig) * kStr;
        HND_MASS(e.copy)  = HND_MASS(e.orig)  * kMas;

        // Fizika kutlesi handling-den AYRI saxlanir -> onu da yenile.
        veh->m_fMass = e.baseMass * kMas;
    }
}

namespace CVehicleTuning
{
    VehicleTuning Get(CVehicleGTA* veh)
    {
        if (!veh) return VehicleTuning{};
        std::lock_guard<std::mutex> lk(g_mutex);
        if (Entry* e = Find(veh)) return e->tune;
        return VehicleTuning{};
    }

    void Set(CVehicleGTA* veh, const VehicleTuning& t)
    {
        if (!veh) return;
        std::lock_guard<std::mutex> lk(g_mutex);
        Entry& e = FindOrCreate(veh);
        e.tune = t;
        ApplyHandling(veh, e);
    }

    void Reset(CVehicleGTA* veh)
    {
        if (!veh) return;
        std::lock_guard<std::mutex> lk(g_mutex);
        if (Entry* e = Find(veh))
        {
            RestoreHandling(veh, *e);
            g_entries.erase(veh);
        }
    }

    bool GetHeadlightRGB(CVehicleGTA* veh, uint8_t& r, uint8_t& g, uint8_t& b)
    {
        if (!veh) return false;
        std::lock_guard<std::mutex> lk(g_mutex);
        if (g_entries.empty()) return false;

        Entry* e = Find(veh);
        if (!e) return false;

        const uint8_t id = e->tune.headlight;
        if (id == 0 || id >= HEADLIGHT_COLOR_COUNT) return false;

        r = kPalette[id][0];
        g = kPalette[id][1];
        b = kPalette[id][2];
        return true;
    }

    void GetPaletteColor(int id, uint8_t& r, uint8_t& g, uint8_t& b)
    {
        if (id < 0 || id >= HEADLIGHT_COLOR_COUNT) id = 0;
        r = kPalette[id][0];
        g = kPalette[id][1];
        b = kPalette[id][2];
    }
}
