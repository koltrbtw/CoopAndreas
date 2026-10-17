#include "stdafx.h"
#include "CRelevanceManager.h"
#include <network/packets/vehicles.h>

std::vector<CNetworkVehicle*> CNetworkVehicleManager::m_pVehicles;

void CNetworkVehicleManager::Add(CNetworkVehicle* vehicle)
{
    m_pVehicles.push_back(vehicle);
}

void CNetworkVehicleManager::Remove(CNetworkVehicle* vehicle)
{
    auto it = std::find(m_pVehicles.begin(), m_pVehicles.end(), vehicle);
    if (it != m_pVehicles.end())
    {
        m_pVehicles.erase(it);
    }
}

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

void CNetworkVehicleManager::RemoveAllHostedAndNotify(CNetworkPlayer* player)
{
    for (auto it = CNetworkVehicleManager::m_pVehicles.begin(); it != CNetworkVehicleManager::m_pVehicles.end();)
    {
        if ((*it)->m_pPendingSyncer == player)
        {
            (*it)->m_pPendingSyncer = nullptr;
        }

        // an ambient vehicle goes to the nearest player instead of disappearing in front of him
        CNetworkPlayer* pNewSyncer = nullptr;
        if ((*it)->m_pSyncer == player && (*it)->m_nCreatedBy != MISSION_VEHICLE)
        {
            pNewSyncer = CNetworkPlayerManager::GetNearestPlayer(
                (*it)->m_vecPosition, CNetworkPlayerManager::HANDOVER_ON_LEAVE_DISTANCE, player, (*it)->m_vRelevantTo);
        }

        if (pNewSyncer)
        {
            (*it)->m_pSyncer = pNewSyncer;

            // the server doesn't know the autopilot: cruise and let the new syncer pick the rest
            Packets::Vehicles::VehicleHandoff handoff{};
            handoff.vehicleid = (*it)->m_vehicleId;
            handoff.released = true;
            handoff.hasTransform = false;
            handoff.carMission = MISSION_CRUISE;
            GetPacketFactory().Send(handoff, pNewSyncer);
            ++it;
        }
        else if ((*it)->m_pSyncer == player)
        {
            Packets::Vehicles::VehicleRemove packet{};
            packet.vehicleid = (*it)->m_vehicleId;
            CRelevanceManager::SendToRelevant(packet, (*it)->m_vRelevantTo, player);

            delete *it;
            it = CNetworkVehicleManager::m_pVehicles.erase(it);
        }
        else
        {
            ++it;
        }
    }
}