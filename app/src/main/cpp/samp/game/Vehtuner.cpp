// ============================================================================
//  VehTuner.cpp  (YENI FAYL -> game/ qovluguna qoy, CMake-e elave et)
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
#include "VehTuner.h"
#include "Entity/CVehicleGTA.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Sahe adlari tHandlingData.h ile TEYID OLUNUB (m_fMass, m_fBrakeDeceleration, m_fSteeringLock).
// ---------------------------------------------------------------------------
#define HND_MASS(h)   ((h)->m_fMass)
#define HND_BRAKE(h)  ((h)->m_fBrakeDeceleration)
#define HND_STEER(h)  ((h)->m_fSteeringLock)

namespace
{
    // 0 = standart (override yoxdur, yalniz UI-da boz numune kimi gorunur)
    constexpr uint8_t kPalette[VEHTUNER_COLOR_COUNT][3] = {
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
        VehTune  tune;
    };

    std::unordered_map<CVehicleGTA*, Entry> g_entries;
    std::mutex                              g_mutex;

    inline float Clamp(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

    inline bool Near100(float v) { return std::fabs(v - 100.0f) < 0.5f; }

    inline bool IsNeutral(const VehTune& t)
    {
        return Near100(t.speedPct) && Near100(t.accelPct) && Near100(t.inertiaPct) && Near100(t.massPct) &&
               Near100(t.dragPct) && Near100(t.brakePct) && Near100(t.steerPct) && Near100(t.tractionPct) &&
               Near100(t.tracLossPct) && Near100(t.turnPct) && Near100(t.suspForcePct) &&
               Near100(t.suspDampPct) && Near100(t.collisionPct) &&
               Near100(t.suspUpperPct) && Near100(t.suspLowerPct) && Near100(t.suspBiasPct) &&
               Near100(t.antiDivePct) && Near100(t.tractBiasPct) && Near100(t.brakeBiasPct) &&
               std::fabs(t.comZOffset) < 0.5f;
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
        VehTune& t = e.tune;
        t.speedPct    = Clamp(t.speedPct,    50.0f, 250.0f);
        t.accelPct    = Clamp(t.accelPct,    30.0f, 400.0f);
        t.inertiaPct  = Clamp(t.inertiaPct,  20.0f, 300.0f);
        t.massPct     = Clamp(t.massPct,     30.0f, 300.0f);
        t.dragPct     = Clamp(t.dragPct,     20.0f, 300.0f);
        t.brakePct    = Clamp(t.brakePct,    30.0f, 300.0f);
        t.steerPct    = Clamp(t.steerPct,    50.0f, 150.0f);
        t.tractionPct = Clamp(t.tractionPct, 50.0f, 200.0f);
        t.tracLossPct = Clamp(t.tracLossPct, 20.0f, 200.0f);
        t.turnPct     = Clamp(t.turnPct,     30.0f, 300.0f);
        t.suspForcePct= Clamp(t.suspForcePct,30.0f, 300.0f);
        t.suspDampPct = Clamp(t.suspDampPct, 30.0f, 300.0f);
        t.collisionPct= Clamp(t.collisionPct,10.0f, 300.0f);
        t.suspUpperPct= Clamp(t.suspUpperPct,30.0f, 300.0f);
        t.suspLowerPct= Clamp(t.suspLowerPct,30.0f, 300.0f);
        t.suspBiasPct = Clamp(t.suspBiasPct, 50.0f, 150.0f);
        t.antiDivePct = Clamp(t.antiDivePct,  0.0f, 300.0f);
        t.tractBiasPct= Clamp(t.tractBiasPct,50.0f, 150.0f);
        t.brakeBiasPct= Clamp(t.brakeBiasPct,50.0f, 150.0f);
        t.comZOffset  = Clamp(t.comZOffset, -50.0f,  50.0f);
        if (t.headlight >= VEHTUNER_COLOR_COUNT) t.headlight = 0;

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
        const float kIne = t.inertiaPct * 0.01f;
        const float kMas = t.massPct  * 0.01f;
        const float kDrg = t.dragPct  * 0.01f;
        const float kBrk = t.brakePct * 0.01f;
        const float kStr = t.steerPct * 0.01f;
        const float kTrc = t.tractionPct * 0.01f;
        const float kTrl = t.tracLossPct * 0.01f;
        const float kTrn = t.turnPct  * 0.01f;

        // Her zaman ORIGINAL-dan hesablayiriq -> tekrar tetbiq edende yigilmir (idempotent).
        const cTransmission& to = e.orig->m_transmissionData;
        cTransmission&       tc = e.copy->m_transmissionData;

        tc.m_fMaxVelocity           = to.m_fMaxVelocity        * kSpd;
        tc.m_fMaxGearVelocity       = to.m_fMaxGearVelocity    * kSpd;
        tc.m_maxReverseGearVelocity = to.m_maxReverseGearVelocity;
        tc.m_fEngineAcceleration    = to.m_fEngineAcceleration * kAcc;
        tc.m_fEngineInertia         = to.m_fEngineInertia      * kIne;

        // Vites cedveli: her tTransmissionGear-in butun sahələri suretdir -> hamisini miqyasla.
        static_assert(sizeof(tTransmissionGear) % sizeof(float) == 0, "tTransmissionGear float deyil");
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
        e.copy->m_fMassRecpr = e.orig->m_fMassRecpr / kMas;            // 1/kutle ayrica saxlanir
        e.copy->m_fTurnMass  = e.orig->m_fTurnMass * kMas * kTrn;      // kutle ile birge + oz slideri
        e.copy->m_fDragMult  = e.orig->m_fDragMult * kDrg;

        e.copy->m_fTractionMultiplier = e.orig->m_fTractionMultiplier * kTrc;
        e.copy->m_fTractionLoss       = e.orig->m_fTractionLoss       * kTrl;

        e.copy->m_fSuspensionForceLevel   = e.orig->m_fSuspensionForceLevel   * (t.suspForcePct * 0.01f);
        e.copy->m_fSuspensionDampingLevel = e.orig->m_fSuspensionDampingLevel * (t.suspDampPct  * 0.01f);
        e.copy->m_fCollisionDamageMultiplier = e.orig->m_fCollisionDamageMultiplier * (t.collisionPct * 0.01f);

        // TG_PATCH10: asma sinirlari, balanslar, agirliq merkezi (hamisi ORIGINAL-dan hesablanir)
        e.copy->m_fSuspensionUpperLimit = e.orig->m_fSuspensionUpperLimit * (t.suspUpperPct * 0.01f);
        e.copy->m_fSuspensionLowerLimit = e.orig->m_fSuspensionLowerLimit * (t.suspLowerPct * 0.01f);
        e.copy->m_fSuspensionBiasBetweenFrontAndRear =
            Clamp(e.orig->m_fSuspensionBiasBetweenFrontAndRear * (t.suspBiasPct * 0.01f), 0.05f, 0.95f);
        e.copy->m_fSuspensionAntiDiveMultiplier = e.orig->m_fSuspensionAntiDiveMultiplier * (t.antiDivePct * 0.01f);
        e.copy->m_fTractionBias = Clamp(e.orig->m_fTractionBias * (t.tractBiasPct * 0.01f), 0.05f, 0.95f);
        e.copy->m_fBrakeBias    = Clamp(e.orig->m_fBrakeBias    * (t.brakeBiasPct * 0.01f), 0.05f, 0.95f);
        e.copy->m_vecCentreOfMass.z = e.orig->m_vecCentreOfMass.z + t.comZOffset * 0.01f;

        // Fizika kutlesi handling-den AYRI saxlanir -> onu da yenile.
        veh->m_fMass = e.baseMass * kMas;
    }
}

namespace VehTuner
{
    VehTune Get(CVehicleGTA* veh)
    {
        if (!veh) return VehTune{};
        std::lock_guard<std::mutex> lk(g_mutex);
        if (Entry* e = Find(veh)) return e->tune;
        return VehTune{};
    }

    void Set(CVehicleGTA* veh, const VehTune& t)
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
        if (id == 0 || id >= VEHTUNER_COLOR_COUNT) return false;

        r = kPalette[id][0];
        g = kPalette[id][1];
        b = kPalette[id][2];
        return true;
    }

    void GetPaletteColor(int id, uint8_t& r, uint8_t& g, uint8_t& b)
    {
        if (id < 0 || id >= VEHTUNER_COLOR_COUNT) id = 0;
        r = kPalette[id][0];
        g = kPalette[id][1];
        b = kPalette[id][2];
    }
}


// ===== TG_PATCH10: slotlar + handling kodu =====
#include <string>
#include <cstdio>
#include <cstring>
#include <cmath>
extern char* g_pszStorage;

namespace
{
    constexpr int VT_NF = 20;

    void VtFields(VehTune& t, float** f)
    {
        f[0]  = &t.speedPct;     f[1]  = &t.accelPct;    f[2]  = &t.inertiaPct;   f[3]  = &t.massPct;
        f[4]  = &t.dragPct;      f[5]  = &t.brakePct;    f[6]  = &t.steerPct;     f[7]  = &t.tractionPct;
        f[8]  = &t.tracLossPct;  f[9]  = &t.turnPct;     f[10] = &t.suspForcePct; f[11] = &t.suspDampPct;
        f[12] = &t.collisionPct; f[13] = &t.suspUpperPct; f[14] = &t.suspLowerPct; f[15] = &t.suspBiasPct;
        f[16] = &t.antiDivePct;  f[17] = &t.tractBiasPct; f[18] = &t.brakeBiasPct; f[19] = &t.comZOffset;
    }

    std::string VtPath(const char* file)
    {
        return std::string(g_pszStorage ? g_pszStorage : "") + "SAMP/" + file;
    }

    int VtHex(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    std::string VtEncode(const VehTune& src)
    {
        VehTune t = src;
        float* f[VT_NF];
        VtFields(t, f);
        std::string s = "T1";
        for (int i = 0; i < VT_NF; ++i) {
            uint32_t b;
            memcpy(&b, f[i], 4);
            char h[9];
            snprintf(h, sizeof(h), "%08X", b);
            s += h;
        }
        return s;
    }

    bool VtDecode(const std::string& code, VehTune& out)
    {
        if (code.size() != (size_t)(2 + 8 * VT_NF) || code[0] != 'T' || code[1] != '1') return false;
        VehTune t = out;
        float* f[VT_NF];
        VtFields(t, f);
        for (int i = 0; i < VT_NF; ++i) {
            uint32_t b = 0;
            for (int k = 0; k < 8; ++k) {
                int h = VtHex(code[2 + i * 8 + k]);
                if (h < 0) return false;
                b = (b << 4) | (uint32_t)h;
            }
            float v;
            memcpy(&v, &b, 4);
            if (!std::isfinite(v) || v < -1000.0f || v > 1000.0f) return false;
            *f[i] = v;
        }
        out = t;
        return true;
    }

    std::string g_vtSlots[VehTuner::VEHTUNER_SLOTS + 1];
    bool        g_vtSlotsLoaded = false;

    void VtSlotsLoad()
    {
        if (g_vtSlotsLoaded) return;
        g_vtSlotsLoaded = true;
        FILE* f = fopen(VtPath("vehtune_slots.txt").c_str(), "r");
        if (!f) return;
        char line[400];
        while (fgets(line, sizeof(line), f)) {
            int slot = 0;
            char code[300];
            if (sscanf(line, "%d %299s", &slot, code) != 2) continue;
            if (slot < 1 || slot > VehTuner::VEHTUNER_SLOTS) continue;
            VehTune probe;
            if (VtDecode(code, probe)) g_vtSlots[slot] = code;
        }
        fclose(f);
    }

    void VtSlotsWrite()
    {
        FILE* f = fopen(VtPath("vehtune_slots.txt").c_str(), "w");
        if (!f) return;
        for (int i = 1; i <= VehTuner::VEHTUNER_SLOTS; ++i)
            if (!g_vtSlots[i].empty()) fprintf(f, "%d %s\n", i, g_vtSlots[i].c_str());
        fclose(f);
    }
}

namespace VehTuner
{
    bool SaveSlot(int slot, const VehTune& t)
    {
        if (slot < 1 || slot > VEHTUNER_SLOTS) return false;
        VtSlotsLoad();
        g_vtSlots[slot] = VtEncode(t);
        VtSlotsWrite();
        return true;
    }

    bool LoadSlot(int slot, VehTune& t)
    {
        if (slot < 1 || slot > VEHTUNER_SLOTS) return false;
        VtSlotsLoad();
        if (g_vtSlots[slot].empty()) return false;
        return VtDecode(g_vtSlots[slot], t);
    }

    bool SlotUsed(int slot)
    {
        if (slot < 1 || slot > VEHTUNER_SLOTS) return false;
        VtSlotsLoad();
        return !g_vtSlots[slot].empty();
    }

    std::string ExportCode(const VehTune& t)
    {
        std::string c = VtEncode(t);
        FILE* f = fopen(VtPath("vehtune_code.txt").c_str(), "w");
        if (f) { fprintf(f, "%s\n", c.c_str()); fclose(f); }
        return c;
    }

    bool ParseCode(const std::string& code, VehTune& t) { return VtDecode(code, t); }

    bool ImportCodeFile(VehTune& t)
    {
        FILE* f = fopen(VtPath("vehtune_code.txt").c_str(), "r");
        if (!f) return false;
        char buf[400];
        int ok = fscanf(f, "%399s", buf);
        fclose(f);
        if (ok != 1) return false;
        return VtDecode(buf, t);
    }
}
