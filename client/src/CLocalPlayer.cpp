#include "stdafx.h"

void CLocalPlayer::BuildTaskPacket(eTaskType type, bool toggle)
{
	CPlayerPed* pPlayerPed = FindPlayerPed(0);
	Packets::Players::SetPlayerTask packet{};
	packet.taskType = type;
	packet.vecPos = pPlayerPed->GetPosition();
	packet.currentRotation = pPlayerPed->m_fCurrentRotation;
	packet.aimingRotation = pPlayerPed->m_fAimingRotation;
	packet.toggle = toggle;
	GetPacketFactory().Send(packet);
}

bool CLocalPlayer::GetIsHostingEntity(CEntity* pEntity)
{
    if (pEntity->m_nType == ENTITY_TYPE_PED)
    {
        CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pEntity);
        if (pNetworkPlayer)
        {
            return false;
        }

        CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pEntity);
        if (!pNetworkPed)
        {
            return true;
        }
        if (pNetworkPed->m_bSyncing)
        {
            return true;
        }
    }
    else if (pEntity->m_nType == ENTITY_TYPE_VEHICLE)
    {
        CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pEntity);
        if (!pNetworkVehicle)
        {
            return true;
        }
        if (pNetworkVehicle && pNetworkVehicle->m_bSyncing)
        {
            return true;
        }
    }
    else
    {
        return true;
    }

    return false;
}
// getting in a vehicle (CPed::RemoveWeaponWhenEnteringVehicle 0x5E6370) is the one time the game picks the gun for the
// drive-bys: a Uzi, Tec9 or MP5 goes in hand, the weapon change is locked till we get out. Given one in the vehicle (a
// mission's script for the drive-by part) it stayed in the slot, our hands empty until we got in again. Picked the same
// way here, while no gun was picked yet (m_nSavedWeapon)
void CLocalPlayer::ProcessVehicleWeapon()
{
    constexpr int WEAPON_UNIDENTIFIED = 55;  // eWeaponType of gta-reversed: no weapon saved
    constexpr int WEAPON_SLOT_SMG = 4;
    CPlayerPed* pPlayer = FindPlayerPed(0);
    if (!pPlayer || !pPlayer->m_nPedFlags.bInVehicle || !pPlayer->m_pVehicle ||
        pPlayer->m_nSavedWeapon != WEAPON_UNIDENTIFIED || !CWorld::Players[0].m_bCanDoDriveBy)
    {
        return;
    }

    const CWeapon& smg = pPlayer->m_aWeapons[WEAPON_SLOT_SMG];
    bool bDriveByGun = smg.m_eWeaponType == WEAPON_MICRO_UZI || smg.m_eWeaponType == WEAPON_TEC9 ||
                       smg.m_eWeaponType == WEAPON_MP5;
    if (!bDriveByGun || smg.m_nTotalAmmo <= 0 || pPlayer->m_nActiveWeaponSlot == WEAPON_SLOT_SMG)
    {
        return;
    }

    plugin::CallMethod<0x5E6370, CPed*, int>(pPlayer, 0);  // CPed::RemoveWeaponWhenEnteringVehicle, not a jetpack
    logger::info("a drive-by gun given in the vehicle taken in hand: %d", smg.m_eWeaponType);
}
