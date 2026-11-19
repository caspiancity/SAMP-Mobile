#include "../main.h"
#include "game.h"
#include "../net/netgame.h"
#include "vehicle.h"
#include "Streaming.h"
#include "VehControls.h"

#include <cstdlib>
#include <cstring>
#include <unordered_map>

extern CGame* pGame;
extern CNetGame* pNetGame;

bool bInProcessDetachTrailer = false;

// ============================================================================
//  HANDLING (her masin ucun ferdi, realtime)
// ============================================================================

// CVehicleGTA daxilinde handling pointerinin offseti
#define VEH_HANDLING_PTR_OFFSET   (VER_x32 ? 0x384 : 0x4B0)
#define VEH_HEADLIGHT_OFFSET      (VER_x32 ? 0x588 : 0x6C0)

// tHandlingData daxilinde offsetler (struktura gore: sizeof == 0xE0)
#define HND_SIZE                  0xE0
#define HND_MASS                  0x04   // m_fMass
#define HND_ENGINE_ACCEL          0x7C   // cTransmission::m_fEngineAcceleration
#define HND_MAX_VELOCITY          0x84   // cTransmission::m_fMaxGearVelocity
#define HND_BRAKE_DECEL           0x94   // m_fBrakeDeceleration
#define HND_STEERING_LOCK         0xA0   // m_fSteeringLock

struct HandlingEntry
{
    uintptr_t pOriginal; // oyunun orijinal handling pointeri
    uintptr_t pOwn;      // bu masina aid kopya
};

// CVehicleGTA* -> bu masinin oz handling kopyasi
static std::unordered_map<uintptr_t, HandlingEntry> g_VehicleHandlings;

static inline uintptr_t* GetHandlingSlot(void* pVeh)
{
    return (uintptr_t*)((uintptr_t)pVeh + VEH_HANDLING_PTR_OFFSET);
}

// Masinin oz handling kopyasini qaytarir (yoxdursa yaradir). Ugursuzdursa 0.
static uintptr_t GetOrCreateOwnHandling(void* pVeh)
{
    if (!pVeh) return 0;

    uintptr_t* pSlot = GetHandlingSlot(pVeh);
    uintptr_t pCurrent = *pSlot;
    if (!pCurrent) return 0;

    auto it = g_VehicleHandlings.find((uintptr_t)pVeh);
    if (it != g_VehicleHandlings.end())
    {
        // artiq oz kopyasi var ve masin hele de ondan istifade edir
        if (it->second.pOwn == pCurrent)
            return pCurrent;

        // oyun pointeri deyismisdir (masin yeniden yaradilib ve s.) -> kohne kopyani at
        free((void*)it->second.pOwn);
        g_VehicleHandlings.erase(it);
    }

    uintptr_t pNew = (uintptr_t)malloc(HND_SIZE);
    if (!pNew) return 0;

    memcpy((void*)pNew, (void*)pCurrent, HND_SIZE);

    HandlingEntry entry;
    entry.pOriginal = pCurrent;
    entry.pOwn = pNew;
    g_VehicleHandlings[(uintptr_t)pVeh] = entry;

    *pSlot = pNew;
    return pNew;
}

// Masin silinende orijinal pointeri qaytar ve kopyani azad et
static void ReleaseOwnHandling(void* pVeh)
{
    auto it = g_VehicleHandlings.find((uintptr_t)pVeh);
    if (it == g_VehicleHandlings.end()) return;

    if (pVeh)
    {
        uintptr_t* pSlot = GetHandlingSlot(pVeh);
        if (*pSlot == it->second.pOwn)
            *pSlot = it->second.pOriginal;
    }

    free((void*)it->second.pOwn);
    g_VehicleHandlings.erase(it);
}

static inline uintptr_t GetCurrentHandling(void* pVeh)
{
    if (!pVeh) return 0;
    return *GetHandlingSlot(pVeh);
}

// ============================================================================

CVehicle::CVehicle(int iType, float fX, float fY, float fZ, float fRotation, bool bPreloaded, bool bSiren)
{
    RwMatrix matEnt;
    static CVehicleGTA* pCreatedTrain = nullptr;

    m_pVehicle = nullptr;
    m_dwGTAId = 0;
    m_pTrailer = nullptr;

    if ((iType != TRAIN_PASSENGER_LOCO) &&
        (iType != TRAIN_FREIGHT_LOCO) &&
        (iType != TRAIN_PASSENGER) &&
        (iType != TRAIN_FREIGHT) &&
        (iType != TRAIN_TRAM)) {

        if (!CStreaming::TryLoadModel(iType))
            throw std::runtime_error("Model not loaded");

        ScriptCommand(&create_car, iType, fX, fY, fZ, &m_dwGTAId);
        ScriptCommand(&set_car_z_angle, m_dwGTAId, fRotation);
        ScriptCommand(&car_gas_tank_explosion, m_dwGTAId, 0);
        ScriptCommand(&set_car_hydraulics, m_dwGTAId, 0);
        ScriptCommand(&toggle_car_tires_vulnerable, m_dwGTAId, 0);

        m_pVehicle = GamePool_Vehicle_GetAt(m_dwGTAId);

        if (m_pVehicle)
        {
            m_pVehicle->m_nDoorLock = (eCarLock)0;
            m_bIsLocked = false;

            m_pVehicle->GetMatrix(&matEnt);
            matEnt.pos.x = fX;
            matEnt.pos.y = fY;
            matEnt.pos.z = fZ;

            if (GetVehicleSubtype() != VEHICLE_SUBTYPE_BIKE &&
                GetVehicleSubtype() != VEHICLE_SUBTYPE_PUSHBIKE)
                matEnt.pos.z += 0.25f;

            m_pVehicle->SetMatrix((CMatrix&)matEnt);
            m_bPreloaded = bPreloaded;
        }
    }
    else if ((iType == TRAIN_PASSENGER_LOCO) ||
        (iType == TRAIN_FREIGHT_LOCO) ||
        (iType == TRAIN_TRAM))
    {
        if (iType == TRAIN_PASSENGER_LOCO) iType = 5;
        else if (iType == TRAIN_FREIGHT_LOCO) iType = 3;
        else if (iType == TRAIN_TRAM) iType = 9;

        uint32_t dwDirection = 0;
        if (fRotation > 180.0f) {
            dwDirection = 1;
        }

        if (!CStreaming::TryLoadModel(TRAIN_PASSENGER_LOCO))
            throw std::runtime_error("Model not loaded");

        if (!CStreaming::TryLoadModel(TRAIN_PASSENGER))
            throw std::runtime_error("Model not loaded");

        if (!CStreaming::TryLoadModel(TRAIN_FREIGHT_LOCO))
            throw std::runtime_error("Model not loaded");

        if (!CStreaming::TryLoadModel(TRAIN_FREIGHT))
            throw std::runtime_error("Model not loaded");
        if (!CStreaming::TryLoadModel(TRAIN_TRAM))
            throw std::runtime_error("Model not loaded");

        ScriptCommand(&create_train, iType, fX, fY, fZ, dwDirection, &m_dwGTAId);
        m_pVehicle = GamePool_Vehicle_GetAt(m_dwGTAId);

        pCreatedTrain = m_pVehicle;

        GamePrepareTrain(m_pVehicle);
    }
    else if ((iType == TRAIN_PASSENGER) ||
        (iType == TRAIN_FREIGHT))
    {
        if (!pCreatedTrain)
        {
            m_pVehicle = nullptr;
            return;
        }

        m_dwGTAId = GamePool_Vehicle_GetIndex(m_pVehicle);
        pCreatedTrain = m_pVehicle;
    }

    m_bIsInvulnerable = false;
    m_byteObjectiveVehicle = 0;
    m_bSpecialMarkerEnabled = false;
    m_dwMarkerID = 0;
    m_iEngineState = -1;
    m_iLightState = -1;
    m_bDoorsLocked = false;
    m_bHaveColor = false;
    m_bHasBeenDriven = false;
    m_dwTimeSinceLastDriven = GetTickCount();
    memset(m_szPlateText, 0, sizeof(m_szPlateText));
}

CVehicle::~CVehicle()
{
    m_pVehicle = GamePool_Vehicle_GetAt(m_dwGTAId);

    if (m_pVehicle) {
        auto modelId = m_pVehicle->m_nModelIndex;
        if (m_dwMarkerID)
        {
            pGame->DisableMarker(m_dwMarkerID);
            m_dwMarkerID = 0;
        }

        RemoveEveryoneFromVehicle();

        if (SirenEnabled()) EnableSiren(false);

        if (m_pTrailer) {
            DetachTrailer();
            m_pTrailer = nullptr;
        }

        // Ferdi handling kopyasini sil, orijinal pointeri geri qaytar
        ReleaseOwnHandling(m_pVehicle);
        VehControls::Forget(m_pVehicle);

        int iModel = m_pVehicle->m_nModelIndex;
        if (iModel == 538 || iModel == 537)
        {
            ScriptCommand(&destroy_train, m_dwGTAId);
        }
        else
        {
            ScriptCommand(&destroy_car, m_dwGTAId);
        }

        CStreaming::RemoveModelIfNoRefs(modelId);
    }
}

bool CVehicle::IsRCVehicle()
{
    if (m_pVehicle)
    {
        if (GamePool_Vehicle_GetAt(m_dwGTAId))
        {
            int iModel = m_pVehicle->m_nModelIndex;
            if (iModel == 441
                || iModel == 464
                || iModel == 465
                || iModel == 594
                || iModel == 501
                || iModel == 564) {
                return true;
            }
        }
    }

    return false;
}

float CVehicle::GetHealth()
{
    if (m_pVehicle) {
        return m_pVehicle->fHealth;
    }

    return 0.0f;
}

void CVehicle::SetHealth(float fHealth)
{
    if (m_pVehicle) {
        m_pVehicle->fHealth = fHealth;
    }
}

int CVehicle::GetVehicleSubtype()
{
    if (m_pVehicle) {
        return ::GetVehicleSubtype(m_pVehicle);
    }

    return 0;
}

#include <unistd.h>
void CVehicle::AddComponent(int iComponentID)
{
    if (!m_pVehicle || !GamePool_Vehicle_GetAt(m_dwGTAId)) return;
    if (GetVehicleSubtype() != VEHICLE_SUBTYPE_CAR) return;
    if (iComponentID < 1000 || iComponentID > 1193) return;
    if (m_pVehicle->HasUpgrade(iComponentID)) return;

    // MM_V8: SA-MP-in standart yolu. Evvelki versiyada native AddVehicleUpgrade cagirilirdi -> atma verirdi.
    ScriptCommand(&request_car_component, iComponentID);
    ScriptCommand(&load_requested_models);
    int wait = 0;
    while (!ScriptCommand(&is_component_available, iComponentID)) {
        usleep(2000);
        if (++wait > 400) return;      // ~0.8 san
    }
    uint32_t dwRet = 0;
    ScriptCommand(&add_car_component, m_dwGTAId, iComponentID, &dwRet);
}

void CVehicle::SetPaintJob(uint8_t bytePaintJobID)
{
    if (m_pVehicle && GamePool_Vehicle_GetAt(m_dwGTAId))
    {
        if (GetVehicleSubtype() == VEHICLE_SUBTYPE_CAR)
        {
            if (bytePaintJobID <= 3) {
                if (m_dwGTAId) ScriptCommand(&change_car_skin, m_dwGTAId, bytePaintJobID);
            }
        }
    }
}

void CVehicle::SetColor(uint8_t byteColor1, uint8_t byteColor2)
{
    if (m_pVehicle && GamePool_Vehicle_GetAt(m_dwGTAId))
    {
        m_pVehicle->m_nPrimaryColor = byteColor1;
        m_pVehicle->m_nSecondaryColor = byteColor2;
    }

    m_byteColor1 = byteColor1;
    m_byteColor2 = byteColor2;
    m_bHaveColor = true;
}

void CVehicle::DetachTrailer()
{
    bInProcessDetachTrailer = true;
    if (m_pTrailer)
    {
        if (m_dwGTAId && GamePool_Vehicle_GetAt(m_dwGTAId))
        {
            if (m_pTrailer->m_pVehicle)
                ScriptCommand(&detach_trailer_from_cab, m_pTrailer->m_dwGTAId, m_dwGTAId);
        }
    }
}

void CVehicle::SetTrailer(CVehicle* pTrailer)
{
    m_pTrailer = pTrailer;
}

void CVehicle::LinkToInterior(int iInterior)
{
    if (m_pVehicle)
        m_pVehicle->SetInterior(iInterior);
}

void CVehicle::SetDamageStatus(uint32_t dwPanelDamageStatus, uint32_t dwDoorDamageStatus, uint8_t byteLightDamageStatus)
{

}

void CVehicle::SetTireDamageStatus(uint8_t byteTireDamageStatus)
{

}

void CVehicle::RemoveEveryoneFromVehicle()
{
    if (!m_pVehicle) return;
    if (!GamePool_Vehicle_GetAt(m_dwGTAId)) return;

    if (m_pVehicle->pDriver)
    {
        m_pVehicle->pDriver->RemoveFromVehicle();
    }

    for (int i = 0; i < 7; i++)
    {
        if (m_pVehicle->m_apPassengers[i] != nullptr)
        {
            m_pVehicle->m_apPassengers[i]->RemoveFromVehicle();
        }
    }
}

CVehicle* CVehicle::GetTrailer()
{
    if (m_pVehicle)
    {
        CVehicleGTA* pTrailer = m_pVehicle->m_pTrailer;
        if (pTrailer)
        {
            if (pNetGame)
            {
                CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
                if (pVehiclePool) {
                    VEHICLEID TrailerID = pVehiclePool->FindIDFromGtaPtr(pTrailer);
                    if (TrailerID != INVALID_VEHICLE_ID)
                    {
                        return pVehiclePool->GetAt(TrailerID);
                    }
                }
            }
        }
    }

    return nullptr;
}

CVehicle* CVehicle::GetTractor()
{
    if (!m_pVehicle) return nullptr;

    int iSubType = ::GetVehicleSubtype(m_pVehicle);
    if (iSubType == VEHICLE_SUBTYPE_BIKE
        || iSubType == VEHICLE_SUBTYPE_BOAT
        || iSubType == VEHICLE_SUBTYPE_TRAIN
        || iSubType == VEHICLE_SUBTYPE_PUSHBIKE)
    {
        return nullptr;
    }

    CVehicleGTA* pTractor = m_pVehicle->m_pTowingVehicle;
    if (pTractor)
    {
        if (pNetGame)
        {
            CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
            if (pVehiclePool)
            {
                VEHICLEID TrailerID = pVehiclePool->FindIDFromGtaPtr(pTractor);
                if (TrailerID != INVALID_VEHICLE_ID)
                {
                    return pVehiclePool->GetAt(TrailerID);
                }
            }
        }
    }

    return nullptr;
}

float CVehicle::GetTrainSpeed()
{
    return 0.0f;
}

uint16_t CVehicle::GetHydraThrusters()
{
    return 0.0f;
}

bool CVehicle::IsLandingGearNotUp()
{
    return 0;
}

float CVehicle::GetBikeLean()
{
    return 0.0f;
}

bool CVehicle::IsATrainPart()
{
    if (m_pVehicle)
    {
        int iModelIndex = m_pVehicle->m_nModelIndex;
        if (iModelIndex == 538 || iModelIndex == 570 || iModelIndex == 537 ||
            iModelIndex == 569 || iModelIndex == 449) {
            return true;
        }
    }

    return false;
}

bool CVehicle::VerifyInstance()
{
    return GamePool_Vehicle_GetAt(m_dwGTAId) != nullptr;
}

void CVehicle::SetDoorState(int state)
{
    if (!m_pVehicle) return;

    if (state)
    {
        m_pVehicle->m_nDoorLock = (eCarLock)2;
        m_bDoorsLocked = true;
    }
    else
    {
        m_pVehicle->m_nDoorLock = (eCarLock)0;
        m_bDoorsLocked = false;
    }
}

void CVehicle::SetComponentOpenState(int iDoor, int iComponent, float fDoorOpenRatio)
{
    if (GetVehicleSubtype() == VEHICLE_SUBTYPE_CAR)
    {
        ScriptCommand(&set_car_door_rotation_to, m_dwGTAId, iDoor, iComponent, fDoorOpenRatio);
    }
}

void CVehicle::OpenWindow(uint8_t component) {}
void CVehicle::CloseWindow(uint8_t component) {}

void CVehicle::AttachTrailer()
{
    if (m_pTrailer) {
        ScriptCommand(&put_trailer_on_cab, m_pTrailer->m_dwGTAId, m_dwGTAId);
    }
}

void CVehicle::RemoveComponent(int iComponentID)
{
    if (!m_pVehicle || !GamePool_Vehicle_GetAt(m_dwGTAId))
        return;

    ScriptCommand(&remove_component, m_dwGTAId, iComponentID);
}

void CVehicle::SetZAngle(float fAngle)
{
    if (GamePool_Vehicle_GetAt(m_dwGTAId)) {
        ScriptCommand(&set_car_z_angle, m_dwGTAId, fAngle);
    }
}

bool CVehicle::IsDriverLocalPlayer()
{
    if (m_pVehicle && m_pVehicle->pDriver == GamePool_FindPlayerPed())
        return true;

    return false;
}

void CVehicle::SetInvulnerable(bool bInv)
{
    if (m_pVehicle && GamePool_Vehicle_GetAt(m_dwGTAId))
    {
        if (bInv)
        {
            ScriptCommand(&set_car_immunities, m_dwGTAId, 1, 1, 1, 1, 1);
            ScriptCommand(&toggle_car_tires_vulnerable, m_dwGTAId, 0);
            m_bIsInvulnerable = true;
        }
        else
        {
            ScriptCommand(&set_car_immunities, m_dwGTAId, 0, 0, 0, 0, 0);
            ScriptCommand(&toggle_car_tires_vulnerable, m_dwGTAId, 1);
            m_bIsInvulnerable = false;
        }
    }
}

bool CVehicle::HasSunk()
{
    if (m_pVehicle) {
        return ScriptCommand(&has_car_sunk, m_dwGTAId);
    }

    return false;
}

bool CVehicle::HasADriver()
{
    if (m_pVehicle && GamePool_Vehicle_GetAt(m_dwGTAId))
    {
        if (m_pVehicle->pDriver)
        {
            if (m_pVehicle->pDriver->IsInVehicle() &&
                m_pVehicle->pDriver->m_nPedType == (ePedType)0)
                return true;
        }
    }

    return false;
}

void CVehicle::ApplyEngineState(int iState)
{
    if (!m_pVehicle) return;
    m_iEngineState = iState;
    m_pVehicle->m_nVehicleFlags.bEngineOn = iState;
}

void CVehicle::ApplyLightState(int iState)
{
    if (!m_pVehicle) return;
    m_iLightState = iState;
    m_pVehicle->m_nVehicleFlags.bLightsOn = iState;
}

void CVehicle::ProcessMarkers()
{
    if (!m_pVehicle || !GamePool_Vehicle_GetAt(m_dwGTAId))
        return;

    if (m_byteObjectiveVehicle)
    {
        if (!m_bSpecialMarkerEnabled)
        {
            if (m_dwMarkerID)
            {
                pGame->DisableMarker(m_dwMarkerID);
                m_dwMarkerID = 0;
            }

            ScriptCommand(&tie_marker_to_car, m_dwGTAId, 1, 3, &m_dwMarkerID);
            ScriptCommand(&set_marker_color, m_dwMarkerID, 1006);
            ScriptCommand(&show_on_radar, m_dwMarkerID, 3);
            m_bSpecialMarkerEnabled = true;
        }

        return;
    }

    if (m_byteObjectiveVehicle && m_bSpecialMarkerEnabled)
    {
        if (m_dwMarkerID)
        {
            pGame->DisableMarker(m_dwMarkerID);
            m_dwMarkerID = 0;
            m_bSpecialMarkerEnabled = false;
        }
    }

    if (m_pVehicle->GetDistanceFromLocalPlayerPed() < 200.0f && GetTractor() == NULL &&
        !IsOccupied())
    {
        if (!m_dwMarkerID)
        {
            ScriptCommand(&tie_marker_to_car, m_dwGTAId, 1, 2, &m_dwMarkerID);
            ScriptCommand(&set_marker_color, m_dwMarkerID, 1004);
        }
    }

    else if (IsOccupied() || GetTractor() != NULL ||
        m_pVehicle->GetDistanceFromLocalPlayerPed() >= 200.0f)
    {
        if (m_dwMarkerID)
        {
            pGame->DisableMarker(m_dwMarkerID);
            m_dwMarkerID = 0;
        }
    }
}

bool CVehicle::IsOccupied()
{
    if (m_pVehicle)
    {
        if (m_pVehicle &&
            (m_pVehicle->m_apPassengers[0] ||
                m_pVehicle->m_apPassengers[1] ||
                m_pVehicle->m_apPassengers[2] ||
                m_pVehicle->m_apPassengers[3] ||
                m_pVehicle->m_apPassengers[4] ||
                m_pVehicle->m_apPassengers[5] ||
                m_pVehicle->m_apPassengers[6])) {
            return true;
        }
    }

    return false;
}

bool CVehicle::IsATowTruck()
{
    if (!m_pVehicle) return false;
    return m_pVehicle->m_nModelIndex == 525;
}

bool CVehicle::IsATrailer()
{
    if (!m_pVehicle) return false;

    int nModel = m_pVehicle->m_nModelIndex;
    return (nModel == 435 ||
        nModel == 450 ||
        nModel == 584 ||
        nModel == 591 ||
        nModel == 606 ||
        nModel == 607 ||
        nModel == 608 ||
        nModel == 610 ||
        nModel == 611
        );
}

void CVehicle::UpdateDamageStatus(uint32_t dwPanelDamage, uint32_t dwDoorDamage, uint8_t byteLightDamage)
{
    if (m_pVehicle)
    {
        if (GetVehicleSubtype() == VEHICLE_SUBTYPE_CAR)
        {
            if (!dwPanelDamage && !dwDoorDamage && !byteLightDamage)
            {
                if (GetPanelDamageStatus() || GetDoorDamageStatus() || GetLightDamageStatus())
                {
                    ((void (*)(CVehicleGTA*))(g_libGTASA + (VER_x32 ? 0x55D5C0 + 1 : 0x67DF0C)))(m_pVehicle);
                    return;
                }
            }

            SetDoorDamageStatus(dwDoorDamage);
            SetPanelDamageStatus(dwPanelDamage);
            SetLightDamageStatus(byteLightDamage);

            ((void (*)(CVehicleGTA*))(g_libGTASA + (VER_x32 ? 0x55D886 + 1 : 0x67E368)))(m_pVehicle);
        }
    }
}

void CVehicle::SetPanelDamageStatus(uint32_t dwPanelStatus) {}
uint32_t CVehicle::GetPanelDamageStatus() { return 0; }
void CVehicle::SetDoorDamageStatus(uint32_t dwDoorStatus) {}
uint32_t CVehicle::GetDoorDamageStatus() { return 0; }
void CVehicle::SetLightDamageStatus(uint8_t byteLightStatus) {}
uint8_t CVehicle::GetLightDamageStatus() { return 0; }
void CVehicle::SetWheelPoppedStatus(uint8_t byteWheelStatus) {}
uint8_t CVehicle::GetWheelPoppedStatus() { return 0; }

bool CVehicle::SirenEnabled()
{
    // DUZELDILDI: evvel "if(!m_pVehicle)" idi -> nullptr deref
    if (m_pVehicle)
        return m_pVehicle->m_nVehicleFlags.bSirenOrAlarm;

    return false;
}

void CVehicle::EnableSiren(bool bState)
{
    if (!m_pVehicle) return;

    m_pVehicle->m_nVehicleFlags.bSirenOrAlarm = (int)bState;
}

void CVehicle::UpdateColor()
{
    if (m_pVehicle)
    {
        if (m_bHaveColor)
        {
            if (!m_bHasBeenDriven)
            {
                if (m_pVehicle->m_nPrimaryColor != m_byteColor1 || m_pVehicle->m_nSecondaryColor != m_byteColor2)
                {
                    m_pVehicle->m_nPrimaryColor = m_byteColor1;
                    m_pVehicle->m_nSecondaryColor = m_byteColor2;
                }
            }
        }
    }
}

bool CVehicle::UpdateLastDrivenTime()
{
    if (m_pVehicle)
    {
        if (m_pVehicle->pDriver)
        {
            m_bHasBeenDriven = true;
            m_dwTimeSinceLastDriven = GetTickCount();
            return true;
        }
    }

    return false;
}

// ===== HER MASIN UCUN FERDI HANDLING VE FAR FUNKSIYALARI =====

void CVehicle::EnsureUniqueHandling()
{
    if (!m_pVehicle) return;
    GetOrCreateOwnHandling(m_pVehicle);
}

void CVehicle::SetMaxSpeed(float fSpeed)
{
    if (!m_pVehicle) return;
    uintptr_t pHandling = GetOrCreateOwnHandling(m_pVehicle);
    if (pHandling) {
        *(float*)(pHandling + HND_MAX_VELOCITY) = fSpeed;
    }
}

void CVehicle::SetAcceleration(float fAccel)
{
    if (!m_pVehicle) return;
    uintptr_t pHandling = GetOrCreateOwnHandling(m_pVehicle);
    if (pHandling) {
        *(float*)(pHandling + HND_ENGINE_ACCEL) = fAccel;
    }
}

void CVehicle::SetBrakePower(float fBrake)
{
    if (!m_pVehicle) return;
    uintptr_t pHandling = GetOrCreateOwnHandling(m_pVehicle);
    if (pHandling) {
        *(float*)(pHandling + HND_BRAKE_DECEL) = fBrake;
    }
}

void CVehicle::SetSteeringAngle(float fAngle)
{
    if (!m_pVehicle) return;
    uintptr_t pHandling = GetOrCreateOwnHandling(m_pVehicle);
    if (pHandling) {
        *(float*)(pHandling + HND_STEERING_LOCK) = fAngle;
    }
}

void CVehicle::SetMass(float fMass)
{
    if (!m_pVehicle) return;
    uintptr_t pHandling = GetOrCreateOwnHandling(m_pVehicle);
    if (pHandling && fMass > 0.0f) {
        *(float*)(pHandling + HND_MASS) = fMass;
        *(float*)(pHandling + 0x08) = 1.0f / fMass; // m_fMassRecpr
    }
}

void CVehicle::SetHeadlightColor(uint8_t colorID)
{
    if (!m_pVehicle) return;
    *(uint8_t*)((uintptr_t)m_pVehicle + VEH_HEADLIGHT_OFFSET) = colorID;
}

float CVehicle::GetMaxSpeed() {
    uintptr_t pHandling = GetCurrentHandling(m_pVehicle);
    return pHandling ? *(float*)(pHandling + HND_MAX_VELOCITY) : 0.0f;
}

float CVehicle::GetAcceleration() {
    uintptr_t pHandling = GetCurrentHandling(m_pVehicle);
    return pHandling ? *(float*)(pHandling + HND_ENGINE_ACCEL) : 0.0f;
}

float CVehicle::GetBrakePower() {
    uintptr_t pHandling = GetCurrentHandling(m_pVehicle);
    return pHandling ? *(float*)(pHandling + HND_BRAKE_DECEL) : 0.0f;
}

float CVehicle::GetSteeringAngle() {
    uintptr_t pHandling = GetCurrentHandling(m_pVehicle);
    return pHandling ? *(float*)(pHandling + HND_STEERING_LOCK) : 0.0f;
}

float CVehicle::GetMass() {
    uintptr_t pHandling = GetCurrentHandling(m_pVehicle);
    return pHandling ? *(float*)(pHandling + HND_MASS) : 0.0f;
}

void CVehicle::SetPlateText(const char* szPlateText)
{
    if (!szPlateText) return;
    strncpy(m_szPlateText, szPlateText, sizeof(m_szPlateText) - 1);
    m_szPlateText[sizeof(m_szPlateText) - 1] = '\0';
}

// ===== HNDV2: handling redaktoru backend (menyu + slotlar + kod) =====
#include <vector>
#include <map>
#include <string>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <strings.h>
#include "tHandlingData.h"

extern char* g_pszStorage;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"

struct HndxField { const char* name; size_t off; };
#define HNDX(n, m) { n, offsetof(tHandlingData, m) }
static const HndxField g_HndxFields[] = {
    HNDX("mass", m_fMass),
    HNDX("turnmass", m_fTurnMass),
    HNDX("drag", m_fDragMult),
    { "comx", offsetof(tHandlingData, m_vecCentreOfMass) },
    { "comy", offsetof(tHandlingData, m_vecCentreOfMass) + 4 },
    { "comz", offsetof(tHandlingData, m_vecCentreOfMass) + 8 },
    HNDX("buoyancy", m_fBuoyancyConstant),
    HNDX("traction", m_fTractionMultiplier),
    { "accel", HND_ENGINE_ACCEL },
    { "maxspeed", HND_MAX_VELOCITY },
    HNDX("brake", m_fBrakeDeceleration),
    HNDX("brakebias", m_fBrakeBias),
    HNDX("steering", m_fSteeringLock),
    HNDX("tractionloss", m_fTractionLoss),
    HNDX("tractionbias", m_fTractionBias),
    HNDX("suspforce", m_fSuspensionForceLevel),
    HNDX("suspdamp", m_fSuspensionDampingLevel),
    HNDX("suspdampspeed", m_fSuspensionHighSpdComDamp),
    HNDX("suspupper", m_fSuspensionUpperLimit),
    HNDX("susplower", m_fSuspensionLowerLimit),
    HNDX("suspbias", m_fSuspensionBiasBetweenFrontAndRear),
    HNDX("suspantidive", m_fSuspensionAntiDiveMultiplier),
    HNDX("collision", m_fCollisionDamageMultiplier),
    HNDX("seatoffset", m_fSeatOffsetDistance),
};
static const int HNDX_COUNT = (int)(sizeof(g_HndxFields) / sizeof(g_HndxFields[0]));

static const HndxField* HndxFind(const char* n)
{
    if (!n) return nullptr;
    for (int i = 0; i < HNDX_COUNT; i++)
        if (strcasecmp(g_HndxFields[i].name, n) == 0) return &g_HndxFields[i];
    return nullptr;
}

static void HndxFixMass(uintptr_t p)
{
    const float m = *(float*)(p + offsetof(tHandlingData, m_fMass));
    if (m > 0.0f) *(float*)(p + offsetof(tHandlingData, m_fMassRecpr)) = 1.0f / m;
}

// masinin ilk (original) handling-i: "Sifirla" ucun
static std::map<CVehicleGTA*, std::vector<uint8_t>> g_HndxOrig;

// MM_V8: ferdi handling kopyasi + CPhysical kutlesi (handling-den AYRI saxlanir)
struct HndxPhys { float mass; float turn; uint32_t ctime; };
static std::map<CVehicleGTA*, HndxPhys> g_HndxPhys;
struct HndxOwn { uint32_t ctime; tHandlingData* copy; };
static std::map<CVehicleGTA*, HndxOwn> g_HndxOwnMap;

// maxspeed: yalniz bir sahe yox, vites cedveli ve maxVelocity de miqyaslanmalidir
static void HndxScaleSpeed(uintptr_t p, float ratio)
{
    if (!(ratio > 0.01f && ratio < 100.0f)) return;
    tHandlingData* h = (tHandlingData*)p;
    cTransmission& t = h->m_transmissionData;
    t.m_fMaxVelocity *= ratio;
    float* gr = (float*)t.m_aGears;
    const size_t n = sizeof(t.m_aGears) / sizeof(float);
    for (size_t i = 0; i < n; ++i) gr[i] *= ratio;
}

static tHandlingData* HndxOwnCopy(CVehicleGTA* g)
{   // MM_V8: pointer birbasa strukturdan (xam offset yox); masina ferdi kopya
    if (!g || !g->m_pHandlingData) return nullptr;
    std::map<CVehicleGTA*, HndxOwn>::iterator it = g_HndxOwnMap.find(g);
    if (it != g_HndxOwnMap.end()) {
        if (it->second.ctime == g->m_nCreationTime && g->m_pHandlingData == it->second.copy) return it->second.copy;
        if (it->second.ctime != g->m_nCreationTime) delete it->second.copy;   // eyni adresde basqa masin
        g_HndxOwnMap.erase(it);
    }
    tHandlingData* c = new tHandlingData(*g->m_pHandlingData);
    g->m_pHandlingData = c;
    HndxOwn o; o.ctime = g->m_nCreationTime; o.copy = c;
    g_HndxOwnMap[g] = o;
    return c;
}

static void HndxSnap(CVehicle* v)
{
    CVehicleGTA* g = v->m_pVehicle;
    if (!g || !g->m_pHandlingData) return;
    std::map<CVehicleGTA*, HndxPhys>::iterator ph = g_HndxPhys.find(g);
    if (ph != g_HndxPhys.end() && ph->second.ctime == g->m_nCreationTime && g_HndxOrig.count(g)) return;
    g_HndxOrig[g].assign((uint8_t*)g->m_pHandlingData, (uint8_t*)g->m_pHandlingData + HND_SIZE);
    HndxPhys pp; pp.mass = g->m_fMass; pp.turn = g->m_fTurnMass; pp.ctime = g->m_nCreationTime;
    g_HndxPhys[g] = pp;
}

static int HndxHexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool HndxParseHex(const char* s, size_t n, std::vector<uint8_t>& out)
{
    if (n % 2) return false;
    out.clear();
    for (size_t i = 0; i < n; i += 2) {
        const int a = HndxHexVal(s[i]), b = HndxHexVal(s[i + 1]);
        if (a < 0 || b < 0) return false;
        out.push_back((uint8_t)(a * 16 + b));
    }
    return true;
}

static std::string HndxPath(const char* file)
{
    return std::string(g_pszStorage ? g_pszStorage : "") + "SAMP/" + file;
}

bool HndExt_Get(CVehicle* v, const char* name, float* out)
{
    if (!v || !v->m_pVehicle || !out) return false;
    const HndxField* f = HndxFind(name);
    if (!f) return false;
    tHandlingData* h = v->m_pVehicle->m_pHandlingData;
    if (!h) return false;
    *out = *(float*)((uintptr_t)h + f->off);
    return true;
}

bool HndExt_Set(CVehicle* v, const char* name, float val)
{
    if (!v || !v->m_pVehicle) return false;
    if (!std::isfinite(val) || val > 1.0e6f || val < -1.0e6f) return false;
    const HndxField* f = HndxFind(name);
    if (!f) return false;
    HndxSnap(v);
    tHandlingData* h = HndxOwnCopy(v->m_pVehicle);
    if (!h) return false;
    const uintptr_t p = (uintptr_t)h;
    CVehicleGTA* gv = v->m_pVehicle;
    const float oldVal = *(float*)(p + f->off);
    *(float*)(p + f->off) = val;
    if (f->off == offsetof(tHandlingData, m_fMass)) {
        HndxFixMass(p);
        if (oldVal > 1.0e-4f) gv->m_fMass *= (val / oldVal);
    }
    if (f->off == offsetof(tHandlingData, m_fTurnMass) && oldVal > 1.0e-4f) gv->m_fTurnMass *= (val / oldVal);
    if (f->off == HND_MAX_VELOCITY && oldVal > 1.0e-4f && val > 0.0f) HndxScaleSpeed(p, val / oldVal);
    if (f->off == offsetof(tHandlingData, m_fSuspensionUpperLimit) ||
        f->off == offsetof(tHandlingData, m_fSuspensionLowerLimit) ||
        f->off == offsetof(tHandlingData, m_fSuspensionBiasBetweenFrontAndRear))
        gv->RefreshSuspensionLines();
    return true;
}

bool HndExt_Reset(CVehicle* v)
{
    if (!v || !v->m_pVehicle) return false;
    std::map<CVehicleGTA*, std::vector<uint8_t>>::iterator it = g_HndxOrig.find(v->m_pVehicle);
    if (it == g_HndxOrig.end() || it->second.size() != HND_SIZE) return false;
    tHandlingData* h = HndxOwnCopy(v->m_pVehicle);
    if (!h) return false;
    memcpy((void*)((uintptr_t)h + 4), it->second.data() + 4, HND_SIZE - 4);
    std::map<CVehicleGTA*, HndxPhys>::iterator ph = g_HndxPhys.find(v->m_pVehicle);
    if (ph != g_HndxPhys.end()) { v->m_pVehicle->m_fMass = ph->second.mass; v->m_pVehicle->m_fTurnMass = ph->second.turn; }
    v->m_pVehicle->RefreshSuspensionLines();
    return true;
}

// ---- slotlar 1..5 : SAMP/handling_slots.txt ----
#define HNDX_SLOTS 5
static std::vector<uint8_t> g_HndxSlots[HNDX_SLOTS + 1];
static bool g_HndxSlotsLoaded = false;

static void HndxSlotsWrite()
{
    FILE* f = fopen(HndxPath("handling_slots.txt").c_str(), "w");
    if (!f) return;
    for (int i = 1; i <= HNDX_SLOTS; i++) {
        if (g_HndxSlots[i].empty()) continue;
        fprintf(f, "%d ", i);
        for (size_t k = 0; k < g_HndxSlots[i].size(); ++k) fprintf(f, "%02X", g_HndxSlots[i][k]);
        fprintf(f, "\n");
    }
    fclose(f);
}

static void HndxSlotsRead()
{
    if (g_HndxSlotsLoaded) return;
    g_HndxSlotsLoaded = true;
    FILE* f = fopen(HndxPath("handling_slots.txt").c_str(), "r");
    if (!f) return;
    static char line[1024];
    static char hex[900];
    while (fgets(line, sizeof(line), f)) {
        int slot = 0;
        if (sscanf(line, "%d %899s", &slot, hex) != 2) continue;
        if (slot < 1 || slot > HNDX_SLOTS) continue;
        std::vector<uint8_t> data;
        if (HndxParseHex(hex, strlen(hex), data) && data.size() == HND_SIZE) g_HndxSlots[slot] = data;
    }
    fclose(f);
}

static bool HndxOldSlotUsed(int slot)
{
    if (slot < 1 || slot > HNDX_SLOTS) return false;
    HndxSlotsRead();
    return g_HndxSlots[slot].size() == HND_SIZE;
}

bool HndExt_SaveSlot(CVehicle* v, int slot)
{
    if (!v || !v->m_pVehicle || slot < 1 || slot > HNDX_SLOTS) return false;
    const uintptr_t p = GetCurrentHandling(v->m_pVehicle);
    if (!p) return false;
    HndxSlotsRead();
    g_HndxSlots[slot].assign((uint8_t*)p, (uint8_t*)p + HND_SIZE);
    HndxSlotsWrite();
    return true;
}

bool HndExt_LoadSlot(CVehicle* v, int slot)
{
    if (!v || !v->m_pVehicle || !HndxOldSlotUsed(slot)) return false;
    HndxSnap(v);
    const uintptr_t p = GetOrCreateOwnHandling(v->m_pVehicle);
    if (!p) return false;
    memcpy((void*)(p + 4), g_HndxSlots[slot].data() + 4, HND_SIZE - 4);   // ilk 4 bayt = model ID
    return true;
}

// ---- handling kodu = butun strukturun hex-i; SAMP/handling_code.txt ----
std::string HndExt_GetCode(CVehicle* v)
{
    if (!v || !v->m_pVehicle) return "";
    const uintptr_t p = GetCurrentHandling(v->m_pVehicle);
    if (!p) return "";
    std::string s = "H2";
    for (size_t i = 4; i < HND_SIZE; ++i) {
        char b[3];
        snprintf(b, sizeof(b), "%02X", ((uint8_t*)p)[i]);
        s += b;
    }
    FILE* f = fopen(HndxPath("handling_code.txt").c_str(), "w");
    if (f) { fprintf(f, "%s\n", s.c_str()); fclose(f); }
    return s;
}

static bool HndxApplyCode(CVehicle* v, const std::string& code)
{
    if (!v || !v->m_pVehicle) return false;
    if (code.size() != (size_t)(2 + 2 * (HND_SIZE - 4)) || code[0] != 'H' || code[1] != '2') return false;
    std::vector<uint8_t> data;
    if (!HndxParseHex(code.c_str() + 2, code.size() - 2, data)) return false;
    HndxSnap(v);
    const uintptr_t p = GetOrCreateOwnHandling(v->m_pVehicle);
    if (!p) return false;
    memcpy((void*)(p + 4), data.data(), data.size());
    HndxFixMass(p);
    return true;
}

bool HndExt_ImportCodeFile(CVehicle* v)
{
    FILE* f = fopen(HndxPath("handling_code.txt").c_str(), "r");
    if (!f) return false;
    char buf[800];
    const int ok = fscanf(f, "%799s", buf);
    fclose(f);
    if (ok != 1) return false;
    return HndxApplyCode(v, buf);
}

#pragma clang diagnostic pop
// ===== /HNDV2 =====

// ===== HNDV3: handling.cfg slotlari (SAMP/hslot_1..5.cfg, launcher yazir) =====
#include <chrono>
#include <cctype>
#include <algorithm>

namespace hv3
{
    static std::map<int, std::string> s_ide;       // model id -> handling adi (vehicles.ide)
    static bool s_ideLoaded = false;

    static std::string Trim(const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && isspace((unsigned char)s[a])) ++a;
        while (b > a && isspace((unsigned char)s[b - 1])) --b;
        return s.substr(a, b - a);
    }

    static std::string Up(std::string s)
    {
        for (size_t i = 0; i < s.size(); ++i) s[i] = (char)toupper((unsigned char)s[i]);
        return s;
    }

    static void Split(const char* line, std::vector<std::string>& out)
    {
        out.clear();
        std::string cur;
        for (const char* c = line; *c; ++c) {
            if (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n') {
                if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            } else cur += *c;
        }
        if (!cur.empty()) out.push_back(cur);
    }

    static void LoadIde()
    {
        if (s_ideLoaded) return;
        s_ideLoaded = true;
        FILE* f = fopen(HndxPath("vehicles.ide").c_str(), "r");
        if (!f) return;
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            if (line[0] < '0' || line[0] > '9') continue;
            std::vector<std::string> tk;
            std::string cur;
            for (const char* c = line; *c; ++c) {
                if (*c == ',') { tk.push_back(Trim(cur)); cur.clear(); }
                else cur += *c;
            }
            tk.push_back(Trim(cur));
            if (tk.size() < 5) continue;
            s_ide[atoi(tk[0].c_str())] = Up(tk[4]);
        }
        fclose(f);
    }

    // standart baza: oyunun ozunun yukledigi fayl
    static std::string BaseCfgPath()
    {
        const std::string c = HndxPath("handling_custom.cfg");
        FILE* f = fopen(c.c_str(), "rb");
        if (f) { fclose(f); return c; }
        return HndxPath("handling.cfg");
    }

    // faylda ad ile setri tapir; qiymetler: adin sonrakisi (0-dan). Reqem olmayanlar NAN.
    static bool FindLine(const std::string& path, const std::string& name, std::vector<float>& vals)
    {
        FILE* f = fopen(path.c_str(), "r");
        if (!f) return false;
        char line[1024];
        std::vector<std::string> tk;
        bool found = false;
        while (fgets(line, sizeof(line), f)) {
            const char* p = line;
            while (*p == ' ' || *p == '\t') ++p;
            if (!*p || *p == ';' || *p == '*' || *p == '!' || *p == '$' || *p == '%' || *p == '^' || *p == '\r' || *p == '\n') continue;
            Split(p, tk);
            if (tk.size() < 2 || Up(tk[0]) != name) continue;
            vals.clear();
            for (size_t i = 1; i < tk.size(); ++i) {
                char* e = nullptr;
                const float v = strtof(tk[i].c_str(), &e);
                vals.push_back((e && e != tk[i].c_str()) ? v : NAN);
            }
            found = true;
            break;
        }
        fclose(f);
        return found;
    }

    // Vahid cevirmesini bilmeye ehtiyac olmasin deye NISBET: yeni = indiki * (fayl_slot / fayl_baza)
    static float Scale(float cur, float base, float imp)
    {
        if (!std::isfinite(base) || !std::isfinite(imp)) return cur;
        if (fabsf(base) > 1.0e-6f) return cur * (imp / base);
        return (fabsf(imp) > 1.0e-6f) ? imp : cur;
    }

    static float Ratio(float base, float imp)
    {
        if (!std::isfinite(base) || !std::isfinite(imp) || fabsf(base) < 1.0e-6f) return 1.0f;
        return imp / base;
    }
}

std::string HndExt_SlotPath(int slot)   // HNDV5_PATH: Java fayl secici bura yazir
{
    char nm[32];
    snprintf(nm, sizeof(nm), "hslot_%d.cfg", (slot < 1 ? 1 : (slot > HNDX_SLOTS ? HNDX_SLOTS : slot)));
    return HndxPath(nm);
}

bool HndExt_SlotUsed(int slot)
{
    if (slot < 1 || slot > HNDX_SLOTS) return false;
    static bool used[HNDX_SLOTS + 1] = {};
    static bool inited = false;
    static std::chrono::steady_clock::time_point last;
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (!inited || std::chrono::duration<float>(now - last).count() > 0.7f) {
        inited = true;
        last = now;
        for (int i = 1; i <= HNDX_SLOTS; ++i) {
            char nm[32];
            snprintf(nm, sizeof(nm), "hslot_%d.cfg", i);
            FILE* f = fopen(HndxPath(nm).c_str(), "rb");
            used[i] = (f != nullptr);
            if (f) fclose(f);
        }
    }
    return used[slot];
}

bool HndExt_ApplyFileSlot(CVehicle* v, int slot, std::string& msg)
{
    msg.clear();
    if (!v || !v->m_pVehicle) { msg = "Avtomobil tapılmadı"; return false; }
    if (slot < 1 || slot > HNDX_SLOTS) { msg = "Yanlış slot"; return false; }

    char nm[32];
    snprintf(nm, sizeof(nm), "hslot_%d.cfg", slot);
    const std::string slotPath = HndxPath(nm);
    {
        FILE* f = fopen(slotPath.c_str(), "rb");
        if (!f) { msg = "Slot boşdur — launcher: Settings → Fayl seç"; return false; }
        fclose(f);
    }

    hv3::LoadIde();
    const int mid = (int)v->m_pVehicle->m_nModelIndex;
    std::map<int, std::string>::iterator it = hv3::s_ide.find(mid);
    if (it == hv3::s_ide.end()) { msg = "Model vehicles.ide-də tapılmadı"; return false; }
    const std::string name = it->second;

    std::vector<float> base, imp;
    if (!hv3::FindLine(hv3::BaseCfgPath(), name, base)) { msg = name + " baza handling-də tapılmadı"; return false; }
    if (!hv3::FindLine(slotPath, name, imp)) { msg = name + " bu faylda yoxdur"; return false; }
    if (base.size() < 29 || imp.size() < 29) { msg = "Sətir natamamdır (" + name + ")"; return false; }
    if (!(imp[0] > 0.0f)) { msg = "Fayldakı çəki yanlışdır"; return false; }

    HndxSnap(v);
    tHandlingData* h = (tHandlingData*)GetOrCreateOwnHandling(v->m_pVehicle);
    if (!h) { msg = "Handling yaradıla bilmədi"; return false; }

    const std::vector<float>& B = base;
    const std::vector<float>& I = imp;
    using hv3::Scale;

    // fayl sutunlari (adin sonrasi, 0-dan):
    // 0 mass 1 turnmass 2 drag 3-5 com 6 submerged 7 tractionMult 8 tractionLoss 9 tractionBias 10 gears
    // 11 maxVel 12 accel 13 inertia 14 drive 15 engine 16 brake 17 brakeBias 18 abs 19 steering
    // 20 suspForce 21 suspDamp 22 suspHighSpd 23 upper 24 lower 25 bias 26 antidive 27 seatOffset 28 collision
    const float oldMass = h->m_fMass;
    h->m_fMass = Scale(h->m_fMass, B[0], I[0]);
    if (oldMass > 0.0f && h->m_fMass > 0.0f) v->m_pVehicle->m_fMass *= h->m_fMass / oldMass;
    h->m_fTurnMass = Scale(h->m_fTurnMass, B[1], I[1]);
    h->m_fDragMult = Scale(h->m_fDragMult, B[2], I[2]);
    h->m_vecCentreOfMass.x = Scale(h->m_vecCentreOfMass.x, B[3], I[3]);
    h->m_vecCentreOfMass.y = Scale(h->m_vecCentreOfMass.y, B[4], I[4]);
    h->m_vecCentreOfMass.z = Scale(h->m_vecCentreOfMass.z, B[5], I[5]);

    h->m_fTractionMultiplier = Scale(h->m_fTractionMultiplier, B[7], I[7]);
    h->m_fTractionLoss       = Scale(h->m_fTractionLoss,       B[8], I[8]);
    h->m_fTractionBias       = Scale(h->m_fTractionBias,       B[9], I[9]);

    {   // surat: maksimum suret + vites cedveli birlikde miqyaslanir
        const float kv = hv3::Ratio(B[11], I[11]);
        cTransmission& t = h->m_transmissionData;
        t.m_fMaxVelocity     *= kv;
        t.m_fMaxGearVelocity *= kv;
        constexpr size_t kFloatsPerGear = sizeof(tTransmissionGear) / sizeof(float);
        for (int i = 0; i < 6; ++i) {
            float* g = reinterpret_cast<float*>(&t.m_aGears[i]);
            for (size_t j = 0; j < kFloatsPerGear; ++j) g[j] *= kv;
        }
        t.m_fEngineAcceleration = Scale(t.m_fEngineAcceleration, B[12], I[12]);
        t.m_fEngineInertia      = Scale(t.m_fEngineInertia,      B[13], I[13]);
    }

    h->m_fBrakeDeceleration = Scale(h->m_fBrakeDeceleration, B[16], I[16]);
    h->m_fBrakeBias         = Scale(h->m_fBrakeBias,         B[17], I[17]);
    h->m_fSteeringLock      = Scale(h->m_fSteeringLock,      B[19], I[19]);

    h->m_fSuspensionForceLevel                = Scale(h->m_fSuspensionForceLevel,                B[20], I[20]);
    h->m_fSuspensionDampingLevel              = Scale(h->m_fSuspensionDampingLevel,              B[21], I[21]);
    h->m_fSuspensionHighSpdComDamp            = Scale(h->m_fSuspensionHighSpdComDamp,            B[22], I[22]);
    h->m_fSuspensionUpperLimit                = Scale(h->m_fSuspensionUpperLimit,                B[23], I[23]);
    h->m_fSuspensionLowerLimit                = Scale(h->m_fSuspensionLowerLimit,                B[24], I[24]);
    h->m_fSuspensionBiasBetweenFrontAndRear   = Scale(h->m_fSuspensionBiasBetweenFrontAndRear,   B[25], I[25]);
    h->m_fSuspensionAntiDiveMultiplier        = Scale(h->m_fSuspensionAntiDiveMultiplier,        B[26], I[26]);
    h->m_fSeatOffsetDistance                  = Scale(h->m_fSeatOffsetDistance,                  B[27], I[27]);
    h->m_fCollisionDamageMultiplier           = Scale(h->m_fCollisionDamageMultiplier,           B[28], I[28]);

    HndxFixMass((uintptr_t)h);
    msg = name + " tətbiq edildi (slot " + std::to_string(slot) + ")";
    return true;
}
// ===== /HNDV3 =====

// MM_V8
