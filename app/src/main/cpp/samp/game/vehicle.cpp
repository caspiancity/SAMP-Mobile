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

void CVehicle::AddComponent(int iComponentID)
{
    if (!m_pVehicle || !GamePool_Vehicle_GetAt(m_dwGTAId)) return;
    if (GetVehicleSubtype() != VEHICLE_SUBTYPE_CAR) return;

    if (!CStreaming::TryLoadModel(iComponentID))
        return;

    if (!ScriptCommand(&is_component_available, iComponentID)) {
        return;
    }

    uint32_t dwRet;
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


// ===== HNDEXT_PATCH8: handling sahe sistemi / slotlar / kod / cfg =====
#include <vector>
#include <string>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include "tHandlingData.h"

extern char* g_pszStorage;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"

struct HndExtField { const char* name; size_t off; };
#define HNDX(n, m) { n, offsetof(tHandlingData, m) }
static const HndExtField g_HndExtFields[] = {
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
static const int HNDX_COUNT = (int)(sizeof(g_HndExtFields) / sizeof(g_HndExtFields[0]));

static const HndExtField* HndxFind(const char* n)
{
    if (!n) return nullptr;
    for (int i = 0; i < HNDX_COUNT; i++)
        if (strcasecmp(g_HndExtFields[i].name, n) == 0) return &g_HndExtFields[i];
    return nullptr;
}

static void HndxFixMass(uintptr_t p)
{
    float m = *(float*)(p + offsetof(tHandlingData, m_fMass));
    if (m > 0.0f) *(float*)(p + offsetof(tHandlingData, m_fMassRecpr)) = 1.0f / m;
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
        int a = HndxHexVal(s[i]), b = HndxHexVal(s[i + 1]);
        if (a < 0 || b < 0) return false;
        out.push_back((uint8_t)(a * 16 + b));
    }
    return true;
}

static std::string HndxStorage(const char* file)
{
    return std::string(g_pszStorage ? g_pszStorage : "") + "SAMP/" + file;
}

std::string HndExt_FieldList()
{
    std::string s;
    for (int i = 0; i < HNDX_COUNT; i++) { if (i) s += ", "; s += g_HndExtFields[i].name; }
    return s;
}

bool HndExt_Set(CVehicle* v, const char* name, float val)
{
    if (!v || !v->m_pVehicle) return false;
    const HndExtField* f = HndxFind(name);
    if (!f) return false;
    uintptr_t p = GetOrCreateOwnHandling(v->m_pVehicle);
    if (!p) return false;
    *(float*)(p + f->off) = val;
    if (f->off == offsetof(tHandlingData, m_fMass)) HndxFixMass(p);
    return true;
}

bool HndExt_Get(CVehicle* v, const char* name, float* out)
{
    if (!v || !v->m_pVehicle || !out) return false;
    const HndExtField* f = HndxFind(name);
    if (!f) return false;
    uintptr_t p = GetCurrentHandling(v->m_pVehicle);
    if (!p) return false;
    *out = *(float*)(p + f->off);
    return true;
}

std::vector<std::string> HndExt_DumpLines(CVehicle* v)
{
    std::vector<std::string> lines;
    if (!v || !v->m_pVehicle) return lines;
    uintptr_t p = GetCurrentHandling(v->m_pVehicle);
    if (!p) return lines;
    std::string cur;
    for (int i = 0; i < HNDX_COUNT; i++) {
        char b[64];
        snprintf(b, sizeof(b), "%s=%.3f  ", g_HndExtFields[i].name, *(float*)(p + g_HndExtFields[i].off));
        cur += b;
        if ((i % 5) == 4) { lines.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

// ---- slotlar (1..5), fayla yazilir: SAMP/handling_slots.txt ----
#define HNDX_SLOTS 5
static std::vector<uint8_t> g_HndSlots[HNDX_SLOTS + 1];
static bool g_HndSlotsLoaded = false;

static void HndxSlotsWrite()
{
    FILE* f = fopen(HndxStorage("handling_slots.txt").c_str(), "w");
    if (!f) return;
    for (int i = 1; i <= HNDX_SLOTS; i++) {
        if (g_HndSlots[i].empty()) continue;
        fprintf(f, "%d ", i);
        for (uint8_t b : g_HndSlots[i]) fprintf(f, "%02X", b);
        fprintf(f, "\n");
    }
    fclose(f);
}

static void HndxSlotsRead()
{
    if (g_HndSlotsLoaded) return;
    g_HndSlotsLoaded = true;
    FILE* f = fopen(HndxStorage("handling_slots.txt").c_str(), "r");
    if (!f) return;
    static char line[1024];
    while (fgets(line, sizeof(line), f)) {
        int slot = 0;
        char hex[900];
        if (sscanf(line, "%d %899s", &slot, hex) != 2) continue;
        if (slot < 1 || slot > HNDX_SLOTS) continue;
        std::vector<uint8_t> data;
        if (HndxParseHex(hex, strlen(hex), data) && data.size() == HND_SIZE) g_HndSlots[slot] = data;
    }
    fclose(f);
}

bool HndExt_SaveSlot(CVehicle* v, int slot)
{
    if (!v || !v->m_pVehicle || slot < 1 || slot > HNDX_SLOTS) return false;
    uintptr_t p = GetCurrentHandling(v->m_pVehicle);
    if (!p) return false;
    HndxSlotsRead();
    g_HndSlots[slot].assign((uint8_t*)p, (uint8_t*)p + HND_SIZE);
    HndxSlotsWrite();
    return true;
}

bool HndExt_LoadSlot(CVehicle* v, int slot)
{
    if (!v || !v->m_pVehicle || slot < 1 || slot > HNDX_SLOTS) return false;
    HndxSlotsRead();
    if (g_HndSlots[slot].size() != HND_SIZE) return false;
    uintptr_t p = GetOrCreateOwnHandling(v->m_pVehicle);
    if (!p) return false;
    // ilk 4 bayt = masinin handling ID-si, toxunulmur
    memcpy((void*)(p + 4), g_HndSlots[slot].data() + 4, HND_SIZE - 4);
    return true;
}

// ---- handling kodu: "H1" + her sahe ucun 8 hex (float bitleri) ----
std::string HndExt_GetCode(CVehicle* v)
{
    if (!v || !v->m_pVehicle) return "";
    uintptr_t p = GetCurrentHandling(v->m_pVehicle);
    if (!p) return "";
    std::string s = "H1";
    for (int i = 0; i < HNDX_COUNT; i++) {
        uint32_t bits;
        memcpy(&bits, (void*)(p + g_HndExtFields[i].off), 4);
        char b[9];
        snprintf(b, sizeof(b), "%08X", bits);
        s += b;
    }
    FILE* f = fopen(HndxStorage("handling_code.txt").c_str(), "w");
    if (f) { fprintf(f, "%s\n", s.c_str()); fclose(f); }
    return s;
}

bool HndExt_SetCode(CVehicle* v, const std::string& code)
{
    if (!v || !v->m_pVehicle) return false;
    if (code.size() != (size_t)(2 + 8 * HNDX_COUNT) || code[0] != 'H' || code[1] != '1') return false;
    std::vector<uint8_t> data;
    if (!HndxParseHex(code.c_str() + 2, code.size() - 2, data)) return false;
    uintptr_t p = GetOrCreateOwnHandling(v->m_pVehicle);
    if (!p) return false;
    for (int i = 0; i < HNDX_COUNT; i++) {
        uint32_t bits = ((uint32_t)data[i * 4] << 24) | ((uint32_t)data[i * 4 + 1] << 16) |
                        ((uint32_t)data[i * 4 + 2] << 8) | (uint32_t)data[i * 4 + 3];
        memcpy((void*)(p + g_HndExtFields[i].off), &bits, 4);
    }
    HndxFixMass(p);
    return true;
}

// ---- cfg: SAMP/<ad>.cfg, her setir "sahe=deyer" (; ve # serhdir) ----
// qaytarir: tetbiq olunan sahe sayi, -1 = fayl yoxdur, -2 = yanlis ad
int HndExt_LoadCfg(CVehicle* v, const char* name)
{
    if (!v || !v->m_pVehicle) return -1;
    std::string n = (name && *name) ? name : "handling_user.cfg";
    if (n.find('/') != std::string::npos || n.find("..") != std::string::npos) return -2;
    if (n.size() < 4 || strcasecmp(n.c_str() + n.size() - 4, ".cfg") != 0) n += ".cfg";
    FILE* f = fopen(HndxStorage(n.c_str()).c_str(), "r");
    if (!f) return -1;
    int applied = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* c = strpbrk(line, ";#");
        if (c) *c = 0;
        char key[64];
        float val;
        if (sscanf(line, " %63[^= \t]%*[= \t]%f", key, &val) == 2) {
            if (HndExt_Set(v, key, val)) applied++;
        }
    }
    fclose(f);
    return applied;
}

#pragma clang diagnostic pop
// ===== /HNDEXT_PATCH8 =====
