#pragma once
// ============================================================================
//  vehicle_tuning.h  (YENI FAYL -> game/ qovluguna qoy)
//
//  Ferdi (yalniz bir masina aid) handling + far rengi sistemi.
//  Kohne CVehicle::SetMaxSpeed/SetHeadlightColor ... funksiyalari artiq
//  ISTIFADE OLUNMUR (silmek olar, amma silmek mecburi deyil).
// ============================================================================
#include <cstdint>

struct CVehicleGTA;

// Butun handling deyerleri "faiz"dir: 100 = standart.
// Bu, oyunun daxili vahidlerinden asili olmadan duzgun isleyir.
struct VehicleTuning
{
    float   speedPct  = 100.0f;   // maks. suret      (50 .. 250)
    float   accelPct  = 100.0f;   // tecil            (30 .. 300)
    float   brakePct  = 100.0f;   // tormoz           (30 .. 300)
    float   steerPct  = 100.0f;   // sukan bucagi     (50 .. 150)
    float   massPct   = 100.0f;   // kutle            (30 .. 300)
    uint8_t headlight = 0;        // 0 = standart, 1..15 = palitra rengi
};

constexpr int HEADLIGHT_COLOR_COUNT = 16;

namespace CVehicleTuning
{
    // Masinin hazirki tenzimlemesi (yoxdursa standart).
    VehicleTuning Get(CVehicleGTA* veh);

    // Canli tetbiq edir (hem handling, hem far rengi). Tekrar-tekrar cagirmaq tehlukesizdir.
    void Set(CVehicleGTA* veh, const VehicleTuning& t);

    // Hamisini standarta qaytarir.
    void Reset(CVehicleGTA* veh);

    // Far rengi (DoVehicleLights hook-u istifade edir). Override yoxdursa false.
    bool GetHeadlightRGB(CVehicleGTA* veh, uint8_t& r, uint8_t& g, uint8_t& b);

    // UI ucun palitra rengi (0 = "standart" ucun boz numune).
    void GetPaletteColor(int id, uint8_t& r, uint8_t& g, uint8_t& b);
}
