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

    // UI::render-den cagirilir, ImGui kadrindan EVVEL (tekstura yaradilir)
    void ProcessPending();

    // CAutomobile::CustomCarPlate_BeforeRenderingStart/AfterRenderingStop evezlenir
    void InstallHooks();
}
