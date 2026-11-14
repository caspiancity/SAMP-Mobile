#pragma once
// ============================================================================
//  VehTuner.h  (YENI FAYL -> game/ qovluguna qoy)
//
//  Ferdi (yalniz bir masina aid) handling + far rengi sistemi.
//  Kohne CVehicle::SetMaxSpeed/SetHeadlightColor ... funksiyalari artiq
//  ISTIFADE OLUNMUR (silmek olar, amma silmek mecburi deyil).
// ============================================================================
#include <cstdint>
#include <string>   // TG_PATCH10

struct CVehicleGTA;

// Butun handling deyerleri "faiz"dir: 100 = standart.
// Bu, oyunun daxili vahidlerinden asili olmadan duzgun isleyir.
struct VehTune
{
    // --- Muherrik ---
    float   speedPct    = 100.0f;   // maks. suret                    (50 .. 250)
    float   accelPct    = 100.0f;   // tecil = yerinden goturme       (30 .. 400)
    float   inertiaPct  = 100.0f;   // muherrik ataleti (az = tez)    (20 .. 300)
    float   massPct     = 100.0f;   // kutle                          (30 .. 300)
    float   dragPct     = 100.0f;   // hava muqavimeti                (20 .. 300)
    // --- Idareetme ---
    float   brakePct    = 100.0f;   // tormoz                         (30 .. 300)
    float   steerPct    = 100.0f;   // sukan bucagi                   (50 .. 150)
    float   tractionPct = 100.0f;   // yapisma (traction)             (50 .. 200)
    float   tracLossPct = 100.0f;   // sürüsme itkisi (traction loss) (20 .. 200)
    float   turnPct     = 100.0f;   // firlanma ataleti (kutleye elave)(30 .. 300)
    // --- Asma / zerbe ---
    float   suspForcePct = 100.0f;  // asma gucu                      (30 .. 300)
    float   suspDampPct  = 100.0f;  // asma sonduruculeri             (30 .. 300)
    float   collisionPct = 100.0f;  // toqquşma zererı                (10 .. 300)
    // --- Asma / balans (TG_PATCH10) ---
    float   suspUpperPct = 100.0f;  // asma yuxari hedd               (30 .. 300)
    float   suspLowerPct = 100.0f;  // asma asagi hedd                (30 .. 300)
    float   suspBiasPct  = 100.0f;  // asma on/arxa balansi           (50 .. 150)
    float   antiDivePct  = 100.0f;  // anti-dive                      (0 .. 300)
    float   tractBiasPct = 100.0f;  // yol tutus on/arxa balansi      (50 .. 150)
    float   brakeBiasPct = 100.0f;  // tormoz on/arxa balansi         (50 .. 150)
    float   comZOffset   = 0.0f;    // agirliq merkezi hundurluyu, sm (-50 .. 50), 0 = standart
    // --- Far ---
    uint8_t headlight   = 0;        // 0 = standart, 1..15 = palitra rengi
};

constexpr int VEHTUNER_COLOR_COUNT = 16;

namespace VehTuner
{
    // Masinin hazirki tenzimlemesi (yoxdursa standart).
    VehTune Get(CVehicleGTA* veh);

    // Canli tetbiq edir (hem handling, hem far rengi). Tekrar-tekrar cagirmaq tehlukesizdir.
    void Set(CVehicleGTA* veh, const VehTune& t);

    // Hamisini standarta qaytarir.
    void Reset(CVehicleGTA* veh);

    // Far rengi (DoVehicleLights hook-u istifade edir). Override yoxdursa false.
    bool GetHeadlightRGB(CVehicleGTA* veh, uint8_t& r, uint8_t& g, uint8_t& b);

    // UI ucun palitra rengi (0 = "standart" ucun boz numune).
    void GetPaletteColor(int id, uint8_t& r, uint8_t& g, uint8_t& b);

    // ---- TG_PATCH10: slotlar ve handling kodu ----
    constexpr int VEHTUNER_SLOTS = 5;
    bool        SaveSlot(int slot, const VehTune& t);          // slot 1..5
    bool        LoadSlot(int slot, VehTune& t);                // bos slotda false
    bool        SlotUsed(int slot);
    std::string ExportCode(const VehTune& t);                  // kodu qaytarir + SAMP/vehtune_code.txt-ye yazir
    bool        ParseCode(const std::string& code, VehTune& t);
    bool        ImportCodeFile(VehTune& t);                    // SAMP/vehtune_code.txt-den oxuyur
}
