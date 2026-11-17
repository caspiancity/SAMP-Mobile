#pragma once
// Azerbaycan nomre lovhesi (10-AA-123). Yalniz YERLI ekranda gorunur.
#include <string>

struct CVehicleGTA;

namespace AzPlate
{
    // "10aa123" / "10-AA-123" / "10 aa 123"  ->  "10-AA-123". Format duz deyilse false.
    bool Normalize(const std::string& in, std::string& out);

    // Lovheni masina teyin edir. Masinda lovhe materiali (carplate/nomer) tapilmasa false.
    bool Set(CVehicleGTA* veh, const std::string& normalized);
    void Clear(CVehicleGTA* veh);
    bool Get(CVehicleGTA* veh, std::string& out);
    // /dump: masinin materiallari + dummy-leri <storage>/vehdump.txt faylina (append)
    bool Dump(CVehicleGTA* veh, std::string& outPath);

    // UI::render-den cagirilir, ImGui kadrindan EVVEL (tekstura yaradilir)
    void ProcessPending();

    // CAutomobile::CustomCarPlate_BeforeRenderingStart/AfterRenderingStop evezlenir
    void InstallHooks();
}

// ===== PLATE_V7: PNG nomre lovhesi (3D) =====
struct PlateCfg { float pos[3]; float rot[3]; float w; };

namespace Plate
{
    bool  Normalize(const std::string& in, std::string& out);         // "10aa123" -> "10 AA 123"
    bool  Set(CVehicleGTA* veh, const std::string& text);             // nomreni teyin et
    void  Remove(CVehicleGTA* veh);
    bool  Get(CVehicleGTA* veh, std::string& text);
    void  SetSides(CVehicleGTA* veh, bool front, bool rear);          // qabaq / arxa plaka acigi
    void  GetSides(CVehicleGTA* veh, bool& front, bool& rear);
    void  Render(CVehicleGTA* veh);                                   // CVehicle render hook-undan
    PlateCfg GetCfg(CVehicleGTA* veh, bool front);                    // plates.json (yoxdursa avtomatik)
    void  SetCfg(CVehicleGTA* veh, bool front, const PlateCfg& c);    // yaddasda (modele gore)
    void  ResetCfg(CVehicleGTA* veh, bool front);
    bool  HasCustom(CVehicleGTA* veh, bool front);
    bool  SaveCfg();                                                  // plates.json-a yaz
    bool  WorldPose(CVehicleGTA* veh, bool front, float pos[3], float n[3], float up[3]);
    const char* LastError();
    void* BaseRaster();                                               // RwRaster* (onizleme), yoxdursa nullptr
}
