#include "stdafx.h"
#include "CNetworkVehicle.h"
#include <CKeySync.h>
#include "CNetworkVehicleManager.h"

CNetworkVehicle* CNetworkVehicleManager::GetVehicle(vehicleid_t vehicleid)
{
	if (!vehicleid.valid())
		return nullptr;

	for (int i = 0; i != m_pVehicles.size(); i++)
	{
		if (m_pVehicles[i]->m_vehicleId == vehicleid)
		{
			return m_pVehicles[i];
		}
	}
	return nullptr;
}
CNetworkVehicle* CNetworkVehicleManager::GetVehicle(CEntity* vehicle)
{
	for (int i = 0; i != m_pVehicles.size(); i++)
	{
		if (m_pVehicles[i]->m_pVehicle == vehicle)
		{
			return m_pVehicles[i];
		}
	}

	return nullptr;
}

void CNetworkVehicleManager::Add(CNetworkVehicle* vehicle)
{
	CNetworkVehicleManager::m_pVehicles.push_back(vehicle);
}

void CNetworkVehicleManager::Remove(CNetworkVehicle* vehicle)
{
	auto it = std::find(m_pVehicles.begin(), m_pVehicles.end(), vehicle);
	if (it != m_pVehicles.end())
	{
		m_pVehicles.erase(it);
	}
}

void CNetworkVehicleManager::UpdateDriver(CVehicle* pVehicle)
{
	if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle))
	{
		Packets::Vehicles::VehicleDriverUpdate vehicleDriverUpdate{};
		CPlayerPed* pPlayerPed = FindPlayerPed(0);

		vehicleDriverUpdate.vehicleid = pNetworkVehicle->m_vehicleId;
		vehicleDriverUpdate.pos = pVehicle->m_matrix->pos;
		vehicleDriverUpdate.roll = pVehicle->m_matrix->right;
		vehicleDriverUpdate.rot = pVehicle->m_matrix->up;
		vehicleDriverUpdate.velocity = pVehicle->m_vecMoveSpeed;
		vehicleDriverUpdate.turnSpeed = pVehicle->m_vecTurnSpeed;

		CWeapon& weapon = pPlayerPed->GetWeapon();
		vehicleDriverUpdate.playerWeapon.iWeaponType = weapon.m_eWeaponType;
		vehicleDriverUpdate.playerWeapon.iWeaponState = weapon.m_nState;
		vehicleDriverUpdate.playerWeapon.nAmmo = weapon.m_nAmmoInClip;

		vehicleDriverUpdate.playerHealth.iHealth = static_cast<uint8_t>(std::clamp(pPlayerPed->m_fHealth, 0.0f, 255.0f));
		vehicleDriverUpdate.playerHealth.iArmour = static_cast<uint8_t>(std::clamp(pPlayerPed->m_fArmour, 0.0f, 255.0f));
		CKeySync::CollectState(vehicleDriverUpdate.playerKeys);

		vehicleDriverUpdate.color1 = pVehicle->m_nPrimaryColor;
		vehicleDriverUpdate.color2 = pVehicle->m_nSecondaryColor;

		vehicleDriverUpdate.health = pVehicle->m_fHealth;

		vehicleDriverUpdate.paintjob = pVehicle->GetRemapIndex();

		if (pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
		{
			CAutomobile* pAutomobile = (CAutomobile*)pVehicle;
			vehicleDriverUpdate.miscComponentAngle = pAutomobile->m_wMiscComponentAngle;
		}

		if (pVehicle->m_nVehicleType == VEHICLE_BIKE)
		{
			CBike* pBike = (CBike*)pVehicle;
			vehicleDriverUpdate.bikeLean = pBike->m_rideAnimData.m_fDesiredLeanAngle;
		}

		if (pVehicle->m_nVehicleSubType == VEHICLE_PLANE)
		{
			CPlane* pPlane = (CPlane*)pVehicle;
			vehicleDriverUpdate.planeGearState = pPlane->m_fLandingGearStatus;
		}

		vehicleDriverUpdate.locked = pVehicle->m_eDoorLock;
		vehicleDriverUpdate.hasRotorSpeed = CNetworkVehicle::GetRotorSpeed(pVehicle, &vehicleDriverUpdate.rotorSpeed);
		vehicleDriverUpdate.trailerid = CNetworkVehicle::GetTrailerId(pVehicle);
		vehicleDriverUpdate.radioStation = AudioEngine.GetCurrentRadioStationID();

		GetPacketFactory().Send(vehicleDriverUpdate);
	}
}

// CAutomobile::ProcessControl skips the physics of an abandoned car after 10 frames without a noticeable force
// (CPhysical::m_nFakePhysics, field_B8) and zeroes its speed every frame from then on. Frames with no time step (the
// creation, a pause) reach that before gravity has pulled the car onto its wheels: it hangs in the air for good, for
// the clones too: it stands on its fully extended wheels, a settled car sinks into its suspension (~0.25 m). The
// suspension values can't tell (a skipped car keeps no wheel contact), the height of its collision box above the
// ground can (the box takes the extended wheels); a push down lets the physics run again
static constexpr float MIN_SETTLED_SINK = 0.05f;
static constexpr unsigned int WAKE_INTERVAL = 30000;  // ms, a stiff suspension that barely sinks must not bounce
static constexpr unsigned int MIN_AGE_TO_WAKE = 5000;  // ms since the vehicle was created
static constexpr unsigned int WAKE_DURATION = 3000;   // ms, it sinks slowly enough to be frozen again half way

// the woken vehicles and until when, KeepAwake runs every frame (a pointer is only compared, never read)
static std::vector<std::pair<CVehicle*, unsigned int>> ms_awakeVehicles;

static constexpr unsigned int KEEP_SPEED_TIME = 500;  // ms after the last update, no runaway car when they stop

void CNetworkVehicleManager::AfterProcessControl(CVehicle* pVehicle)
{
	CNetworkVehicle* pNetworkVehicle = GetVehicle(pVehicle);
	if (!pNetworkVehicle || pNetworkVehicle->m_bSyncing || pNetworkVehicle->m_bTrain)
	{
		return;
	}

	pNetworkVehicle->ProcessTargetTransform();

	if (pNetworkVehicle->m_bKeepSpeed)
	{
		if (CTimer::m_snTimeInMilliseconds - pNetworkVehicle->m_nLastTargetTime < KEEP_SPEED_TIME)
		{
			pVehicle->m_vecMoveSpeed = pNetworkVehicle->m_vecKeptMoveSpeed;
			pVehicle->m_vecTurnSpeed = pNetworkVehicle->m_vecKeptTurnSpeed;
		}
		else
		{
			pNetworkVehicle->m_bKeepSpeed = false;
		}
	}

	// the clone's ProcessControl winds it down without the driver's ai or pad
	if (pNetworkVehicle->m_bHasRotorSpeed)
	{
		CNetworkVehicle::SetRotorSpeed(pVehicle, pNetworkVehicle->m_fRotorSpeed);
	}
}

void CNetworkVehicleManager::KeepAwake(CVehicle* pVehicle)
{
	if (ms_awakeVehicles.empty())
	{
		return;
	}

	auto it = std::find_if(ms_awakeVehicles.begin(), ms_awakeVehicles.end(), [pVehicle](auto& e) { return e.first == pVehicle; });
	if (it == ms_awakeVehicles.end())
	{
		return;
	}

	if (CTimer::m_snTimeInMilliseconds > it->second)
	{
		ms_awakeVehicles.erase(it);
		return;
	}
	pVehicle->field_B8 = 0;
}

static void WakeFloatingVehicle(CNetworkVehicle* pNetworkVehicle)
{
	CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
	if (pNetworkVehicle->m_nWakeTime && CTimer::m_snTimeInMilliseconds - pNetworkVehicle->m_nWakeTime < WAKE_INTERVAL)
	{
		return;
	}

	if (pVehicle->m_nVehicleType != VEHICLE_AUTOMOBILE || pVehicle->m_nStatus != STATUS_ABANDONED ||
		pVehicle->field_B8 < 10 || pVehicle->m_vecMoveSpeed.z != 0.0f || pVehicle->m_matrix->at.z < 0.9f)  // at: the up axis in plugin-sdk
	{
		return;
	}

	eVehicleType subType = static_cast<eVehicleType>(pVehicle->m_nVehicleSubType);
	if (subType == VEHICLE_HELI || subType == VEHICLE_PLANE)
	{
		return;
	}

	// woken over a ground without collision yet (the first seconds of a session) it fell through the map
	if (CTimer::m_snTimeInMilliseconds - pVehicle->m_nCreationTime < MIN_AGE_TO_WAKE ||
		!plugin::CallAndReturn<bool, 0x410CE0, const CVector&, int>(pVehicle->GetPosition(), pVehicle->m_nAreaCode))  // CColStore::HasCollisionLoaded
	{
		return;
	}

	CColModel* pColModel = CModelInfo::GetModelInfo(pVehicle->m_nModelIndex)->m_pColModel;
	if (!pColModel)
	{
		return;
	}

	const CVector& pos = pVehicle->GetPosition();
	bool bFound = false;
	float fGroundZ = CWorld::FindGroundZFor3DCoord(pos.x, pos.y, pos.z + 2.0f, &bFound, nullptr);  // from inside the car it finds none
	if (!bFound || pos.z + pColModel->m_boundBox.m_vecMin.z - fGroundZ < -MIN_SETTLED_SINK)
	{
		return;
	}

	pNetworkVehicle->m_nWakeTime = CTimer::m_snTimeInMilliseconds;
	pVehicle->field_B8 = 0;
	pVehicle->m_vecMoveSpeed.z = -0.02f;
	ms_awakeVehicles.emplace_back(pVehicle, CTimer::m_snTimeInMilliseconds + WAKE_DURATION);
	logger::info("vehicle %s woke up from hanging above the ground", to_string(pNetworkVehicle->m_vehicleId).c_str());
}

void CNetworkVehicleManager::UpdateIdle()
{
	CNetworkVehicleManager::RemoveHostedUnused();

	for (auto pNetworkVehicle : m_pVehicles)
	{
		CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
		if (pVehicle == nullptr)
			continue;

		// a train has its own updates, see CTrainSync
		if (pNetworkVehicle->m_bSyncing && !pNetworkVehicle->HasDriver() && !pNetworkVehicle->m_bTrain)
		{
			WakeFloatingVehicle(pNetworkVehicle);

			Packets::Vehicles::VehicleIdleUpdate packet{};
			packet.vehicleid = pNetworkVehicle->m_vehicleId;

			packet.pos = pVehicle->m_matrix->pos;
			packet.roll = pVehicle->m_matrix->right;
			packet.rot = pVehicle->m_matrix->up;
			packet.velocity = pVehicle->m_vecMoveSpeed;
			packet.turnSpeed = pVehicle->m_vecTurnSpeed;
			packet.color1 = pVehicle->m_nPrimaryColor;
			packet.color2 = pVehicle->m_nSecondaryColor;
			packet.health = pVehicle->m_fHealth;
			packet.paintjob = pVehicle->GetRemapIndex();

			if (CUtil::GetVehicleType(pVehicle) == eVehicleType::VEHICLE_PLANE)
			{
				CPlane* plane = (CPlane*)pVehicle;
				packet.planeGearState = plane->m_fLandingGearStatus;
			}

			packet.locked = pVehicle->m_eDoorLock;
			packet.hasRotorSpeed = CNetworkVehicle::GetRotorSpeed(pVehicle, &packet.rotorSpeed);
			GetPacketFactory().Send(packet);
		}
	}
}

void CNetworkVehicleManager::UpdatePassenger(CVehicle* pVehicle, CPlayerPed* pPlayerPed)
{
	 CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle);
	 if (pNetworkVehicle)
	 {
		 Packets::Vehicles::VehiclePassengerUpdate packet{};

		 CWeapon& weapon = pPlayerPed->GetWeapon();
		 packet.playerWeapon.iWeaponType = weapon.m_eWeaponType;
		 packet.playerWeapon.iWeaponState = weapon.m_nState;
		 packet.playerWeapon.nAmmo = weapon.m_nAmmoInClip;

		 packet.playerHealth.iHealth = static_cast<uint8_t>(std::clamp(pPlayerPed->m_fHealth, 0.0f, 255.0f));
		 packet.playerHealth.iArmour = static_cast<uint8_t>(std::clamp(pPlayerPed->m_fArmour, 0.0f, 255.0f));
		 CKeySync::CollectState(packet.playerKeys);

		 packet.vehicleid = pNetworkVehicle->m_vehicleId;
		 packet.driveby = CDriveBy::IsPedInDriveby(pPlayerPed);

		 for (int i = 0; i < pVehicle->m_nMaxPassengers; i++)
		 {
			 if (pVehicle->m_apPassengers[i] == pPlayerPed)
			 {
				 packet.seatid = i;
				 break;
			 }
		 }
		 GetPacketFactory().Send(packet);
	 }
}

vehicleid_t CNetworkVehicleManager::AllocateVehicleId()
{
	assert(CNetworkPlayerManager::m_localPlayerId.valid());
	return vehicleid_t(CNetworkPlayerManager::m_localPlayerId, ms_nNextVehicleId++);
}

void CNetworkVehicleManager::ResetIdAllocator()
{
	ms_nNextVehicleId = 1;
}

void CNetworkVehicleManager::RemoveHostedUnused()
{
	for (auto it = CNetworkVehicleManager::m_pVehicles.begin(); it != CNetworkVehicleManager::m_pVehicles.end();)
	{
		if ((*it)->m_bSyncing)
		{
			CVehicle* vehicle = (*it)->m_pVehicle;
			if (!IsVehiclePointerValid(vehicle))
			{
				delete* it;
				it = m_pVehicles.erase(it);
				continue;
			}
		}
		++it;
	}
}

void CNetworkVehicleManager::UpdateDamageSync()
{
	for (auto pNetworkVehicle : m_pVehicles)
	{
		Packets::Vehicles::VehicleDamage packet{};
		if (pNetworkVehicle->m_bSyncing && pNetworkVehicle->CollectDamage(&packet))
		{
			GetPacketFactory().Send(packet);
		}
	}
}