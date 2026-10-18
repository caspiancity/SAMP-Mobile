#pragma once
// ============================================================================
//  VehPlate.h  (YENI -> game/ qovluguna)
//  Azerbaycan nomre nisani: Java (PlateGen) cizir -> RwTexture -> m_pCustomCarPlate
// ============================================================================
#include <cstdint>

class CVehicle;
struct CVehicleGTA;

namespace VehPlate
{
    // Nomreni teyin et ve (lazim olsa) tekstura sorgusu gonder. Istenilen thread-den cagirila biler.
    void Apply(CVehicle* veh, const char* text);

    // Masin silinende cagir (CVehicle::~CVehicle).
    void OnVehicleDestroy(CVehicleGTA* veh);

    // RENDER thread-de her kadr cagir (UI::render basinda). Gozleyen teksturalari yaradir.
    void Process();
}
