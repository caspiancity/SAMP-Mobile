#pragma once
// ============================================================================
//  VehControls.h  (YENI FAYL -> game/ qovluguna qoy, VehTuner.h-in yanina)
//  Masin sinyal sistemi (sol / sag / avariya). Hazirda yalniz yerli oyuncuya gorunur.
//  Realizasiya: CVehicleGTA.cpp icinde (DoVehicleLights hook-u RenderSignals-i cagirir).
// ============================================================================
#include <cstdint>

struct CVehicleGTA;

enum
{
	VEH_SIGNAL_OFF    = 0,
	VEH_SIGNAL_LEFT   = 1,
	VEH_SIGNAL_RIGHT  = 2,
	VEH_SIGNAL_HAZARD = 3
};

namespace VehControls
{
	uint8_t GetSignal(CVehicleGTA* veh);
	void    SetSignal(CVehicleGTA* veh, uint8_t mode);
	void    ToggleSignal(CVehicleGTA* veh, uint8_t mode);   // eyni rejim yeniden -> sondur
	void    Forget(CVehicleGTA* veh);                       // masin silinende
	bool    BlinkOn();                                      // yanma fazasi (UI de bunu istifade edir)
	void    RenderSignals(CVehicleGTA* veh);                // DoVehicleLights hook-undan cagirilir
}
