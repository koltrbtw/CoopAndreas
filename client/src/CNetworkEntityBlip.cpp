#include "stdafx.h"
#include "CNetworkEntityBlip.h"

static constexpr unsigned int PENDING_TIME = 10000;  // ms, the spawn of a new entity comes within it

static bool IsSameEntity(const CNetworkEntitySerializer& a, const CNetworkEntitySerializer& b)
{
    if (a.entityType != b.entityType)
    {
        return false;
    }
    return a.entityType == eNetworkEntityType::NETWORK_ENTITY_TYPE_PED ? a.pedid == b.pedid : a.vehicleid == b.vehicleid;
}

void CNetworkEntityBlip::DropPending(const CNetworkEntitySerializer& entity)
{
    ms_pending.erase(std::remove_if(ms_pending.begin(), ms_pending.end(),
                         [&](const SPending& pending) { return IsSameEntity(pending.packet.entity, entity); }),
        ms_pending.end());
}

void CNetworkEntityBlip::UpdateEntityBlip(Packets::Blips::UpdateEntityBlip* packet)
{
    DropPending(packet->entity);
    if (!Apply(*packet))
    {
        ms_pending.push_back({*packet, CTimer::m_snTimeInMilliseconds});
    }
}

bool CNetworkEntityBlip::Apply(const Packets::Blips::UpdateEntityBlip& updateBlip)
{
    const Packets::Blips::UpdateEntityBlip* packet = &updateBlip;
    int handle = -1;

    switch (packet->entity.entityType)
    {
        case eNetworkEntityType::NETWORK_ENTITY_TYPE_PED:
            if (auto networkPed = CNetworkPedManager::GetPed(packet->entity.pedid))
            {
                if (auto ped = networkPed->m_pPed)
                {
                    if (networkPed->m_nBlipHandle == -1)
                    {
                        networkPed->m_nBlipHandle =
                            CRadar::SetEntityBlip(BLIP_CHAR, CPools::GetPedRef(ped), 0, packet->display);
                    }
                    handle = networkPed->m_nBlipHandle;
                }
            }
            break;
        case eNetworkEntityType::NETWORK_ENTITY_TYPE_VEHICLE:
            if (auto networkVehicle = CNetworkVehicleManager::GetVehicle(packet->entity.vehicleid))
            {
                if (auto vehicle = networkVehicle->m_pVehicle)
                {
                    if (networkVehicle->m_nBlipHandle == -1)
                    {
                        networkVehicle->m_nBlipHandle =
                            CRadar::SetEntityBlip(BLIP_CAR, CPools::GetVehicleRef(vehicle), 0, packet->display);
                    }
                    handle = networkVehicle->m_nBlipHandle;
                }
            }
            break;
    }

    if (handle >= 0)
    {
        // CChat::AddMessage("%d %d %d %d", packet->scale, packet->color, packet->display, packet->isFriendly);
        CRadar::ChangeBlipScale(handle, packet->scale);
        // TODO(Tornamic): why did i comment this
        // CRadar::ChangeBlipColour(handle, packet->color);
        CRadar::ChangeBlipDisplay(handle, (eBlipDisplay)packet->display);
        CRadar::SetBlipFriendly(handle, packet->isFriendly);
    }
    return handle >= 0;
}

void CNetworkEntityBlip::RemoveEntityBlip(Packets::Blips::RemoveEntityBlip* packet)
{
    DropPending(packet->entity);

    switch (packet->entity.entityType)
    {
        case eNetworkEntityType::NETWORK_ENTITY_TYPE_PED:
            if (auto networkPed = CNetworkPedManager::GetPed(packet->entity.pedid))
            {
                if (auto ped = networkPed->m_pPed)
                {
                    if (networkPed->m_nBlipHandle >= 0)
                    {
                        CRadar::ClearBlip(networkPed->m_nBlipHandle);
                    }
                    networkPed->m_nBlipHandle = -1;
                }
            }
            break;
        case eNetworkEntityType::NETWORK_ENTITY_TYPE_VEHICLE:
            if (auto networkVehicle = CNetworkVehicleManager::GetVehicle(packet->entity.vehicleid))
            {
                if (auto vehicle = networkVehicle->m_pVehicle)
                {
                    if (networkVehicle->m_nBlipHandle >= 0)
                    {
                        CRadar::ClearBlip(networkVehicle->m_nBlipHandle);
                    }
                    networkVehicle->m_nBlipHandle = -1;
                }
            }
            break;
    }
}

void CNetworkEntityBlip::ClearEntityBlips()
{
    ms_pending.clear();

    for (auto pNetworkPed : CNetworkPedManager::m_pPeds)
    {
        if (pNetworkPed->m_nBlipHandle == -1 || !pNetworkPed->m_pPed)
        {
            continue;
        }

        CRadar::ClearBlipForEntity(eBlipType::BLIP_CHAR, CPools::GetPedRef(pNetworkPed->m_pPed));
        pNetworkPed->m_nBlipHandle = -1;
    }

    for (auto pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (pNetworkVehicle->m_nBlipHandle == -1 || !pNetworkVehicle->m_pVehicle)
        {
            continue;
        }

        CRadar::ClearBlipForEntity(eBlipType::BLIP_CAR, CPools::GetVehicleRef(pNetworkVehicle->m_pVehicle));
        pNetworkVehicle->m_nBlipHandle = -1;
    }
}

void CNetworkEntityBlip::Update()
{
    ms_pending.erase(std::remove_if(ms_pending.begin(), ms_pending.end(),
                         [](const SPending& pending) {
                             return CTimer::m_snTimeInMilliseconds - pending.receivedTime > PENDING_TIME ||
                                    Apply(pending.packet);
                         }),
        ms_pending.end());

    /*
        remove blips if the entities are dead
    */
    for (auto pNetworkPed : CNetworkPedManager::m_pPeds)
    {
        if (pNetworkPed->m_nBlipHandle == -1 || !pNetworkPed->m_pPed)
        {
            continue;
        }

        if (pNetworkPed->m_pPed->m_fHealth <= 0.0f)
        {
            CRadar::ClearBlipForEntity(eBlipType::BLIP_CHAR, CPools::GetPedRef(pNetworkPed->m_pPed));
            pNetworkPed->m_nBlipHandle = -1;
        }
    }

    for (auto pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (pNetworkVehicle->m_nBlipHandle == -1 || !pNetworkVehicle->m_pVehicle)
        {
            continue;
        }

        if (pNetworkVehicle->m_pVehicle->m_fHealth <= 0.0f)
        {
            CRadar::ClearBlipForEntity(eBlipType::BLIP_CAR, CPools::GetVehicleRef(pNetworkVehicle->m_pVehicle));
            pNetworkVehicle->m_nBlipHandle = -1;
        }
    }
}
