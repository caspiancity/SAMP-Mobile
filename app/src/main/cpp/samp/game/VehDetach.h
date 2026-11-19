#pragma once
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
