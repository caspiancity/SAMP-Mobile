#include "../main.h"
#include "game.h"
#include "../net/netgame.h"
#include "vehicle.h"
#include "Streaming.h"

extern CGame* pGame;
extern CNetGame* pNetGame;

bool bInProcessDetachTrailer = false;

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

			if( GetVehicleSubtype() != VEHICLE_SUBTYPE_BIKE &&
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
		if(m_dwMarkerID)
		{
			pGame->DisableMarker(m_dwMarkerID);
			m_dwMarkerID = 0;
		}

		RemoveEveryoneFromVehicle();

		if(SirenEnabled()) EnableSiren(false);

		if (m_pTrailer) {
			DetachTrailer();
			m_pTrailer = nullptr;
		}

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
        throw std::runtime_error("Model not loaded");

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
	if(m_pVehicle)
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
	if (state)
	{
		m_pVehicle->m_nDoorLock = (eCarLock)2;
		m_bDoorsLocked = true;
	}
	else
	{
		m_pVehicle->m_nDoorLock =(eCarLock) 0;
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
	if(!m_pVehicle || !GamePool_Vehicle_GetAt(m_dwGTAId))
		return;

	if(m_byteObjectiveVehicle)
	{
		if(!m_bSpecialMarkerEnabled)
		{
			if(m_dwMarkerID)
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

	if(m_byteObjectiveVehicle && m_bSpecialMarkerEnabled)
	{
		if(m_dwMarkerID)
		{
			pGame->DisableMarker(m_dwMarkerID);
			m_dwMarkerID = 0;
			m_bSpecialMarkerEnabled = false;
		}
	}

	if(m_pVehicle->GetDistanceFromLocalPlayerPed() < 200.0f && GetTractor() == NULL &&
	   !IsOccupied())
	{
		if(!m_dwMarkerID)
		{
			ScriptCommand(&tie_marker_to_car, m_dwGTAId, 1, 2, &m_dwMarkerID);
			ScriptCommand(&set_marker_color, m_dwMarkerID, 1004);
		}
	}

	else if(IsOccupied() || GetTractor() != NULL ||
            m_pVehicle->GetDistanceFromLocalPlayerPed() >= 200.0f)
	{
		if(m_dwMarkerID)
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
	if(!m_pVehicle) return false;
	return m_pVehicle->m_nModelIndex == 525;
}

bool CVehicle::IsATrailer()
{
	if(!m_pVehicle) return false;

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
	if(m_pVehicle)
	{
		if(GetVehicleSubtype() == VEHICLE_SUBTYPE_CAR)
		{
			if(!dwPanelDamage && !dwDoorDamage && !byteLightDamage)
			{
				if(GetPanelDamageStatus() || GetDoorDamageStatus() || GetLightDamageStatus())
				{
					(( void (*)(CVehicleGTA*))(g_libGTASA+(VER_x32 ? 0x55D5C0+1:0x67DF0C)))(m_pVehicle);
					return;
				}
			}

			SetDoorDamageStatus(dwDoorDamage);
			SetPanelDamageStatus(dwPanelDamage);
			SetLightDamageStatus(byteLightDamage);

			(( void (*)(CVehicleGTA*))(g_libGTASA+(VER_x32 ? 0x55D886+1:0x67E368)))(m_pVehicle);
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
	if(!m_pVehicle)
		return m_pVehicle->m_nVehicleFlags.bSirenOrAlarm;

	return false;
}

void CVehicle::EnableSiren(bool bState)
{
	if(!m_pVehicle) return;

	m_pVehicle->m_nVehicleFlags.bSirenOrAlarm = (int)bState;
}

void CVehicle::UpdateColor()
{
	if(m_pVehicle)
	{
		if(m_bHaveColor)
		{
			if(!m_bHasBeenDriven)
			{
				if(m_pVehicle->m_nPrimaryColor != m_byteColor1 || m_pVehicle->m_nSecondaryColor != m_byteColor2)
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
	if(m_pVehicle)
	{
		if(m_pVehicle->pDriver)
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

	uintptr_t vehAddr = (uintptr_t)m_pVehicle;
	uintptr_t* ppHandling = (uintptr_t**)(vehAddr + (VER_x32 ? 0x384 : 0x4B0));

	if (ppHandling && *ppHandling) {
		uintptr_t pOldHandling = *ppHandling;
		uintptr_t pNewHandling = (uintptr_t)malloc(0xE0);
		if (pNewHandling) {
			memcpy((void*)pNewHandling, (void*)pOldHandling, 0xE0);
			*ppHandling = pNewHandling;
		}
	}
}

void CVehicle::SetMaxSpeed(float fSpeed)
{
	if (!m_pVehicle) return;
	EnsureUniqueHandling();
	uintptr_t pHandling = *(uintptr_t*)((uintptr_t)m_pVehicle + (VER_x32 ? 0x384 : 0x4B0));
	if (pHandling) {
		*(float*)(pHandling + 0x9C) = fSpeed; // fMaxVelocity
	}
}

void CVehicle::SetAcceleration(float fAccel)
{
	if (!m_pVehicle) return;
	EnsureUniqueHandling();
	uintptr_t pHandling = *(uintptr_t*)((uintptr_t)m_pVehicle + (VER_x32 ? 0x384 : 0x4B0));
	if (pHandling) {
		*(float*)(pHandling + 0x8C) = fAccel; // fEngineAcceleration
	}
}

void CVehicle::SetBrakePower(float fBrake)
{
	if (!m_pVehicle) return;
	EnsureUniqueHandling();
	uintptr_t pHandling = *(uintptr_t*)((uintptr_t)m_pVehicle + (VER_x32 ? 0x384 : 0x4B0));
	if (pHandling) {
		*(float*)(pHandling + 0x94) = fBrake; // fBrakeDeceleration
	}
}

void CVehicle::SetSteeringAngle(float fAngle)
{
	if (!m_pVehicle) return;
	EnsureUniqueHandling();
	uintptr_t pHandling = *(uintptr_t*)((uintptr_t)m_pVehicle + (VER_x32 ? 0x384 : 0x4B0));
	if (pHandling) {
		*(float*)(pHandling + 0xAC) = fAngle; // fSteeringLock
	}
}

void CVehicle::SetMass(float fMass)
{
	if (!m_pVehicle) return;
	EnsureUniqueHandling();
	uintptr_t pHandling = *(uintptr_t*)((uintptr_t)m_pVehicle + (VER_x32 ? 0x384 : 0x4B0));
	if (pHandling) {
		*(float*)(pHandling + 0x04) = fMass; // fMass
	}
}

void CVehicle::SetHeadlightColor(uint8_t colorID)
{
	if (!m_pVehicle) return;
	*(uint8_t*)((uintptr_t)m_pVehicle + (VER_x32 ? 0x588 : 0x6C0)) = colorID;
}

float CVehicle::GetMaxSpeed() {
	if (!m_pVehicle) return 0.0f;
	uintptr_t pHandling = *(uintptr_t*)((uintptr_t)m_pVehicle + (VER_x32 ? 0x384 : 0x4B0));
	return pHandling ? *(float*)(pHandling + 0x9C) : 0.0f;
}

float CVehicle::GetAcceleration() {
	if (!m_pVehicle) return 0.0f;
	uintptr_t pHandling = *(uintptr_t*)((uintptr_t)m_pVehicle + (VER_x32 ? 0x384 : 0x4B0));
	return pHandling ? *(float*)(pHandling + 0x8C) : 0.0f;
}

float CVehicle::GetBrakePower() {
	if (!m_pVehicle) return 0.0f;
	uintptr_t pHandling = *(uintptr_t*)((uintptr_t)m_pVehicle + (VER_x32 ? 0x384 : 0x4B0));
	return pHandling ? *(float*)(pHandling + 0x94) : 0.0f;
}

float CVehicle::GetSteeringAngle() {
	if (!m_pVehicle) return 0.0f;
	uintptr_t pHandling = *(uintptr_t*)((uintptr_t)m_pVehicle + (VER_x32 ? 0x384 : 0x4B0));
	return pHandling ? *(float*)(pHandling + 0xAC) : 0.0f;
}

float CVehicle::GetMass() {
	if (!m_pVehicle) return 0.0f;
	uintptr_t pHandling = *(uintptr_t*)((uintptr_t)m_pVehicle + (VER_x32 ? 0x384 : 0x4B0));
	return pHandling ? *(float*)(pHandling + 0x04) : 0.0f;
}
