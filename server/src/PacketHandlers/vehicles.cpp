#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "network/packets/vehicles.h"
#include "network/packets/peds.h"

PACKET_HANDLER(
    ePacketType::VEHICLE_SPAWN, Packets::Vehicles::VehicleSpawn* pVehicleSpawn, CNetworkPlayer* pNetworkPlayer)
{
    // the id is allocated by its creator, a player can only create vehicles in its own id space
    if (pVehicleSpawn->vehicleid.creator() != pNetworkPlayer->m_playerId)
    {
        logger::warn("%s tried to create a vehicle with someone else's id %s", pNetworkPlayer->GetName().c_str(),
            to_string(pVehicleSpawn->vehicleid).c_str());
        return;
    }

    if (CNetworkVehicleManager::GetVehicle(pVehicleSpawn->vehicleid) != nullptr)
    {
        logger::warn("%s tried to create a vehicle with an id that is already in use %s",
            pNetworkPlayer->GetName().c_str(), to_string(pVehicleSpawn->vehicleid).c_str());
        return;
    }

    GetPacketFactory().SendToAll(*pVehicleSpawn, pNetworkPlayer);

    CNetworkVehicle* vehicle = new CNetworkVehicle(
        pVehicleSpawn->vehicleid, pVehicleSpawn->modelid, pVehicleSpawn->pos, pVehicleSpawn->rot.m_angle);

    vehicle->m_pSyncer = pNetworkPlayer;
    vehicle->m_nPrimaryColor = pVehicleSpawn->color1;
    vehicle->m_nSecondaryColor = pVehicleSpawn->color2;
    vehicle->m_nCreatedBy = pVehicleSpawn->createdBy;

    CNetworkVehicleManager::Add(vehicle);
}

PACKET_HANDLER(
    ePacketType::VEHICLE_REMOVE, Packets::Vehicles::VehicleRemove* pVehicleRemove, CNetworkPlayer* pNetworkPlayer)
{
    if (auto vehicle = CNetworkVehicleManager::GetVehicle(pVehicleRemove->vehicleid))
    {
        if (vehicle->m_pSyncer == pNetworkPlayer)
        {
            GetPacketFactory().SendToAll(*pVehicleRemove, pNetworkPlayer);

            CNetworkVehicleManager::Remove(vehicle);
        }
        else
        {
            logger::warn("%s tried to delete someone else's vehicle", pNetworkPlayer->GetName().c_str());
        }
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_IDLE_UPDATE, Packets::Vehicles::VehicleIdleUpdate* pVehicleIdleUpdate,
    CNetworkPlayer* pNetworkPlayer)
{
    if (auto vehicle = CNetworkVehicleManager::GetVehicle(pVehicleIdleUpdate->vehicleid))
    {
        if (vehicle->m_pSyncer == pNetworkPlayer)
        {
            vehicle->m_bUsedByPed = false;
            vehicle->m_vecPosition = pVehicleIdleUpdate->pos;
            vehicle->m_vecRotation = pVehicleIdleUpdate->rot;
            GetPacketFactory().SendToAll(*pVehicleIdleUpdate, pNetworkPlayer);
        }
        else
        {
            logger::warn("%s tried to update someone else's vehicle (idle)", pNetworkPlayer->GetName().c_str());
        }
    }
    else
    {
        logger::warn("%s tried to update a vehicle (idle) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_DRIVER_UPDATE, Packets::Vehicles::VehicleDriverUpdate* pVehicleDriverUpdate,
    CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleDriverUpdate->vehicleid))
    {
        pNetworkVehicle->SetOccupant(0, pNetworkPlayer);
        pNetworkVehicle->m_vecPosition = pVehicleDriverUpdate->pos;
        pNetworkVehicle->m_vecRotation = pVehicleDriverUpdate->rot;
        pNetworkVehicle->m_bUsedByPed = false;
        pNetworkPlayer->m_vecPosition = pVehicleDriverUpdate->pos;
        pNetworkVehicle->ReassignSyncer(pNetworkPlayer);

        pVehicleDriverUpdate->senderPlayerid = pNetworkPlayer->m_playerId;
        GetPacketFactory().SendToAll(*pVehicleDriverUpdate, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to update a vehicle (driver) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(
    ePacketType::VEHICLE_ENTER, Packets::Vehicles::VehicleEnter* pVehicleEnter, CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleEnter->vehicleid))
    {
        if (pVehicleEnter->bPassenger)
            pNetworkVehicle->SetOccupant(pVehicleEnter->seatid + 1, pNetworkPlayer);
        else
            pNetworkVehicle->SetOccupant(0, pNetworkPlayer);

        pVehicleEnter->senderPlayerid = pNetworkPlayer->m_playerId;
        GetPacketFactory().SendToAll(*pVehicleEnter, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to enter a vehicle that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_EXIT, Packets::Vehicles::VehicleExit* pVehicleExit, CNetworkPlayer* pNetworkPlayer)
{
    pVehicleExit->senderPlayerid = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pVehicleExit, pNetworkPlayer);

    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pNetworkPlayer->m_vehicleId))
    {
        if (pNetworkPlayer->m_nSeatId >= 0)
        {
            pNetworkVehicle->SetOccupant(pNetworkPlayer->m_nSeatId, nullptr);
        }
    }
}

PACKET_HANDLER(
    ePacketType::VEHICLE_DAMAGE, Packets::Vehicles::VehicleDamage* pVehicleDamage, CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleDamage->vehicleid))
    {
        pNetworkVehicle->m_damageManager = pVehicleDamage->damageManager;

        GetPacketFactory().SendToAll(*pVehicleDamage, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to damage a vehicle that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_COMPONENT_ADD, Packets::Vehicles::VehicleComponentAdd* pVehicleComponentAdd,
    CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleComponentAdd->vehicleid))
    {
        pNetworkVehicle->m_pComponents.push_back(pVehicleComponentAdd->componentid);

        GetPacketFactory().SendToAll(*pVehicleComponentAdd, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to sync a vehicle (comp add) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_COMPONENT_REMOVE,
    Packets::Vehicles::VehicleComponentRemove* pVehicleComponentRemove, CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleComponentRemove->vehicleid))
    {
        auto it = std::find(pNetworkVehicle->m_pComponents.begin(), pNetworkVehicle->m_pComponents.end(),
            pVehicleComponentRemove->componentid);
        if (it != pNetworkVehicle->m_pComponents.end())
        {
            pNetworkVehicle->m_pComponents.erase(it);
        }

        GetPacketFactory().SendToAll(*pVehicleComponentRemove, pNetworkPlayer);
    }
    else
    {
        logger::warn("%s tried to sync a vehicle (comp remove) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_PASSENGER_UPDATE,
    Packets::Vehicles::VehiclePassengerUpdate* pVehiclePassengerUpdate, CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehiclePassengerUpdate->vehicleid))
    {
        pVehiclePassengerUpdate->senderPlayerid = pNetworkPlayer->m_playerId;
        GetPacketFactory().SendToAll(*pVehiclePassengerUpdate, pNetworkPlayer);

        pNetworkVehicle->SetOccupant(pVehiclePassengerUpdate->seatid + 1, pNetworkPlayer);
        pNetworkPlayer->m_vecPosition = pNetworkVehicle->m_vecPosition;

        if (pNetworkVehicle->m_nCreatedBy == MISSION_VEHICLE)
        {
            return;
        }

        if (!pNetworkVehicle->m_pPlayers[0] && !pNetworkVehicle->m_bUsedByPed)  // if no driver
        {
            for (uint8_t i = 1; i < 8; i++)
            {
                if (auto pNewSyncer = pNetworkVehicle->m_pPlayers[i])
                {
                    pNetworkVehicle->ReassignSyncer(pNewSyncer);
                    break;
                }
            }
        }
    }
    else
    {
        logger::warn("%s tried to sync a vehicle (passenger) that didn't exist", pNetworkPlayer->GetName().c_str());
    }
}

PACKET_HANDLER(ePacketType::SET_VEHICLE_CREATED_BY, Packets::Vehicles::SetVehicleCreatedBy* pSetVehicleCreatedBy,
    CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer->m_bIsHost)
    {
        if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pSetVehicleCreatedBy->vehicleid))
        {
            pNetworkVehicle->m_nCreatedBy = pSetVehicleCreatedBy->createdBy;
            GetPacketFactory().SendToAll(*pSetVehicleCreatedBy, pNetworkPlayer);
        }
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_REQUEST_OWNERSHIP, Packets::Vehicles::VehicleRequestOwnership* pRequest,
    CNetworkPlayer* pNetworkPlayer)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pRequest->vehicleid);
    if (!pNetworkVehicle || !pNetworkVehicle->m_pSyncer || pNetworkVehicle->m_pSyncer == pNetworkPlayer ||
        pNetworkVehicle->m_pPendingSyncer)
    {
        return;
    }

    // mission vehicles belong to the scripts, vehicles with players inside to their driver
    if (pNetworkVehicle->m_nCreatedBy == MISSION_VEHICLE ||
        std::any_of(std::begin(pNetworkVehicle->m_pPlayers), std::end(pNetworkVehicle->m_pPlayers),
            [](CNetworkPlayer* p) { return p != nullptr; }))
    {
        return;
    }

    pNetworkVehicle->m_pPendingSyncer = pNetworkPlayer;
    GetPacketFactory().Send(*pRequest, pNetworkVehicle->m_pSyncer);
}

PACKET_HANDLER(ePacketType::VEHICLE_HANDOFF, Packets::Vehicles::VehicleHandoff* pHandoff, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pHandoff->vehicleid);
    if (!pNetworkVehicle || pNetworkVehicle->m_pSyncer != pNetworkPlayer)
    {
        return;
    }

    CNetworkPlayer* pNewSyncer = pNetworkVehicle->m_pPendingSyncer;
    pNetworkVehicle->m_pPendingSyncer = nullptr;

    if (!pHandoff->released)
    {
        return;
    }

    // not asked: the syncer gives it away on its own (it would remove it otherwise)
    if (!pNewSyncer && pHandoff->target.valid() && pHandoff->target != pNetworkPlayer->m_playerId)
    {
        pNewSyncer = CNetworkPlayerManager::GetPlayer(pHandoff->target);
    }

    // whoever asked has left meanwhile, the old syncer has to take it back
    if (!pNewSyncer)
    {
        pNewSyncer = pNetworkPlayer;
    }

    pNetworkVehicle->m_pSyncer = pNewSyncer;

    // npc peds inside go together with the vehicle
    for (int i = 0; i < pHandoff->occupantCount; i++)
    {
        CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pHandoff->occupants[i].pedid);
        if (pNetworkPed && pNetworkPed->m_pSyncer == pNetworkPlayer)
        {
            pNetworkPed->m_pSyncer = pNewSyncer;
            pNetworkPed->m_pPendingSyncer = nullptr;
        }
    }

    GetPacketFactory().Send(*pHandoff, pNewSyncer);
}
