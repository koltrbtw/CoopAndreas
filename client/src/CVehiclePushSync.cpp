#include "stdafx.h"
#include "CVehiclePushSync.h"

static constexpr unsigned int REQUEST_INTERVAL = 1500;  // ms, one request per hit series (MTA: 1.5 s too)
static constexpr unsigned int HANDOFF_WAIT = 1000;      // ms, a refused one (a player got in) goes back to its syncer

static bool IsEmpty(CVehicle* pVehicle)
{
    if (pVehicle->m_pDriver)
    {
        return false;
    }
    return std::none_of(std::begin(pVehicle->m_apPassengers), std::end(pVehicle->m_apPassengers),
        [](CPed* pPassenger) { return pPassenger != nullptr; });
}

// the collision is kept on the one that hit (CPhysical::m_pDamageEntity of the moving one): a parked clone skips its
// physics and gets nothing
static void CheckHit(CPhysical* pHitter)
{
    CEntity* pHit = pHitter->m_fDamageIntensity > 0.0f ? pHitter->m_pDamageEntity : nullptr;
    if (!pHit || pHit->m_nType != ENTITY_TYPE_VEHICLE)
    {
        return;
    }

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pHit);
    if (!pNetworkVehicle || pNetworkVehicle->m_bSyncing || pNetworkVehicle->m_bTrain ||
        pNetworkVehicle->m_nCreatedBy == MISSION_VEHICLE || !IsEmpty(pNetworkVehicle->m_pVehicle) ||
        CTimer::m_snTimeInMilliseconds - pNetworkVehicle->m_nPushRequestTime < REQUEST_INTERVAL)
    {
        return;
    }

    pNetworkVehicle->m_nPushRequestTime = CTimer::m_snTimeInMilliseconds;
    Packets::Vehicles::VehicleRequestOwnership packet{};
    packet.vehicleid = pNetworkVehicle->m_vehicleId;
    packet.pushed = true;
    GetPacketFactory().Send(packet);
    logger::info("vehicle %s pushed, requested", to_string(packet.vehicleid).c_str());
}

void CVehiclePushSync::Process()
{
    CPlayerPed* pPlayer = FindPlayerPed(0);
    if (!pPlayer)
    {
        return;
    }

    if (!pPlayer->m_nPedFlags.bInVehicle)
    {
        CheckHit(pPlayer);
    }

    for (CNetworkVehicle* pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (pNetworkVehicle->m_bSyncing && pNetworkVehicle->m_pVehicle && !pNetworkVehicle->m_bTrain)
        {
            CheckHit(pNetworkVehicle->m_pVehicle);
        }
    }
}

bool CVehiclePushSync::IsWaitingForHandoff(CNetworkVehicle* pNetworkVehicle)
{
    return !pNetworkVehicle->m_bSyncing && pNetworkVehicle->m_nPushRequestTime &&
           CTimer::m_snTimeInMilliseconds - pNetworkVehicle->m_nPushRequestTime < HANDOFF_WAIT;
}
