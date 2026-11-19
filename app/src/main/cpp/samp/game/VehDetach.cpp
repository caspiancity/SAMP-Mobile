#include "VehDetach.h"
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
