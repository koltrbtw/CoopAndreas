#include "stdafx.h"
#include "CRelevanceManager.h"
#include <network/packets/vehicles.h>

CNetworkVehicle::CNetworkVehicle(vehicleid_t vehicleid, unsigned short model, CVector pos, float rot)
{
    m_vehicleId = vehicleid;
    m_nModelId = model;
    m_vecPosition = pos;
    m_vecRotation = CVector(0, 0, 0);
    m_vecRight = CVector(std::cos(rot), std::sin(rot), 0.0f);
    m_nLastSyncerPacketTime = g_serverTime;
}

float CNetworkVehicle::GetHeading() const
{
    // right = (cos h, sin h, 0) for CPlaceable::GetHeading's forward (-sin h, cos h, 0)
    return std::atan2(m_vecRight.y, m_vecRight.x);
}

void CNetworkVehicle::ReassignSyncer(CNetworkPlayer* newSyncer)
{
    m_nLastSyncerPacketTime = g_serverTime;
    if (m_pSyncer != newSyncer)
    {
        Packets::Vehicles::AssignVehicleSyncer packet{};
        packet.vehicleid = m_vehicleId;

        // send to the old vehicle syncer
        if (m_pSyncer)
        {
            packet.isSyncer = false;
            GetPacketFactory().Send(packet, m_pSyncer);
        }

        // send to the new
        packet.isSyncer = true;
        GetPacketFactory().Send(packet, newSyncer);

        CRelevanceManager::EnsureRelevant(this, newSyncer);
        m_pSyncer = newSyncer;
        m_pPendingSyncer = nullptr;  // a handoff from the old syncer would be ignored anyway
    }
}

void CNetworkVehicle::SetOccupant(int8_t seatid, CNetworkPlayer* player)
{
    if (seatid < 0 || seatid > 7)
        return;

    if (this->m_pPlayers[seatid])
    {
        this->m_pPlayers[seatid]->m_vehicleId = vehicleid_t();
        this->m_pPlayers[seatid]->m_nSeatId = -1;
    }

    this->m_pPlayers[seatid] = player;

    if (player)
    {
        player->m_vehicleId = this->m_vehicleId;
        player->m_nSeatId = seatid;
    }
}
