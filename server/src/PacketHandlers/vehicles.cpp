#include "network/packet_types.h"
#include "stdafx.h"
#include "CRelevanceManager.h"
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

    CNetworkVehicle* vehicle = new CNetworkVehicle(
        pVehicleSpawn->vehicleid, pVehicleSpawn->modelid, pVehicleSpawn->pos, pVehicleSpawn->rot.m_angle);

    vehicle->m_pSyncer = pNetworkPlayer;
    vehicle->m_nPrimaryColor = pVehicleSpawn->color1;
    vehicle->m_nSecondaryColor = pVehicleSpawn->color2;
    vehicle->m_anExtras[0] = pVehicleSpawn->extras[0];
    vehicle->m_anExtras[1] = pVehicleSpawn->extras[1];
    vehicle->m_nCreatedBy = pVehicleSpawn->createdBy;

    CNetworkVehicleManager::Add(vehicle);
    CRelevanceManager::OnVehicleCreated(vehicle);
}

PACKET_HANDLER(ePacketType::TRAIN_SPAWN, Packets::Vehicles::TrainSpawn* pTrainSpawn, CNetworkPlayer* pNetworkPlayer)
{
    if (pTrainSpawn->vehicleid.creator() != pNetworkPlayer->m_playerId)
    {
        logger::warn("%s tried to create a train with someone else's id %s", pNetworkPlayer->GetName().c_str(),
            to_string(pTrainSpawn->vehicleid).c_str());
        return;
    }

    if (CNetworkVehicleManager::GetVehicle(pTrainSpawn->vehicleid) != nullptr)
    {
        logger::warn("%s tried to create a train with an id that is already in use %s",
            pNetworkPlayer->GetName().c_str(), to_string(pTrainSpawn->vehicleid).c_str());
        return;
    }

    // the model doesn't matter, the receivers build the carriages from the train type
    CNetworkVehicle* vehicle = new CNetworkVehicle(pTrainSpawn->vehicleid, 0, pTrainSpawn->pos, 0.0f);

    vehicle->m_pSyncer = pNetworkPlayer;
    vehicle->m_nCreatedBy = pTrainSpawn->isMission ? MISSION_VEHICLE : RANDOM_VEHICLE;
    vehicle->m_bTrain = true;
    vehicle->m_trainState = pTrainSpawn->state;

    CNetworkVehicleManager::Add(vehicle);
    CRelevanceManager::OnVehicleCreated(vehicle);
}

PACKET_HANDLER(ePacketType::TRAIN_UPDATE, Packets::Vehicles::TrainUpdate* pTrainUpdate, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkVehicle* vehicle = CNetworkVehicleManager::GetVehicle(pTrainUpdate->vehicleid);
    if (!vehicle || !vehicle->m_bTrain || vehicle->m_pSyncer != pNetworkPlayer)
    {
        return;
    }

    vehicle->m_vecPosition = pTrainUpdate->pos;
    vehicle->m_trainState = pTrainUpdate->state;
    CRelevanceManager::SendToRelevant(*pTrainUpdate, vehicle->m_vRelevantTo, pNetworkPlayer);
}

PACKET_HANDLER(
    ePacketType::VEHICLE_REMOVE, Packets::Vehicles::VehicleRemove* pVehicleRemove, CNetworkPlayer* pNetworkPlayer)
{
    if (auto vehicle = CNetworkVehicleManager::GetVehicle(pVehicleRemove->vehicleid))
    {
        if (vehicle->m_pSyncer == pNetworkPlayer)
        {
            // the server time: with the syncer's one a vehicle removed right after its creation (a car generator)
            // came before the spawn the server sent, the others kept the clone (CPacketBuffer plays by serverTime)
            pVehicleRemove->serverTime = g_serverTime;
            CRelevanceManager::SendToRelevant(*pVehicleRemove, vehicle->m_vRelevantTo, pNetworkPlayer);

            CNetworkVehicleManager::Remove(vehicle);
        }
        else
        {
            // a clone the sender dropped, see CNetworkVehicleManager::DropStaleClones on the client: if the vehicle
            // is alive and the sender wants it, the next relevance update spawns it again
            logger::info("%s dropped its clone of %s", pNetworkPlayer->GetName().c_str(),
                to_string(pVehicleRemove->vehicleid).c_str());
            CRelevanceManager::OnCloneDropped(vehicle, pNetworkPlayer);
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
            vehicle->m_nLastSyncerPacketTime = g_serverTime;
            vehicle->m_bUsedByPed = false;
            vehicle->m_vecPosition = pVehicleIdleUpdate->pos;
            vehicle->m_vecRotation = pVehicleIdleUpdate->rot;
            vehicle->m_vecRight = pVehicleIdleUpdate->roll;
            CRelevanceManager::SendToRelevant(*pVehicleIdleUpdate, vehicle->m_vRelevantTo, pNetworkPlayer);
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
        pNetworkVehicle->m_nLastSyncerPacketTime = g_serverTime;
        pNetworkVehicle->m_vecPosition = pVehicleDriverUpdate->pos;
        pNetworkVehicle->m_vecRotation = pVehicleDriverUpdate->rot;
        pNetworkVehicle->m_vecRight = pVehicleDriverUpdate->roll;
        pNetworkVehicle->m_bUsedByPed = false;
        pNetworkPlayer->m_vecPosition = pVehicleDriverUpdate->pos;
        pNetworkPlayer->m_bHasPosition = true;
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

    // the leave task may still give up, the seat is free once the sender is out (any way, not only by the task)
    if (pVehicleExit->reason != Packets::Vehicles::VEHICLE_EXIT_LEAVE)
    {
        pNetworkPlayer->RemoveFromVehicle();
    }
}

PACKET_HANDLER(
    ePacketType::VEHICLE_DAMAGE, Packets::Vehicles::VehicleDamage* pVehicleDamage, CNetworkPlayer* pNetworkPlayer)
{
    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleDamage->vehicleid))
    {
        // the previous syncer may still send its last state after a handoff, it would undo the new one's
        if (pNetworkVehicle->m_pSyncer != pNetworkPlayer)
        {
            return;
        }

        pNetworkVehicle->m_damageManager = pVehicleDamage->damageManager;
        memcpy(pNetworkVehicle->m_aBikeWheelStatus, pVehicleDamage->bikeWheelStatus, sizeof(pNetworkVehicle->m_aBikeWheelStatus));

        CRelevanceManager::SendToRelevant(*pVehicleDamage, pNetworkVehicle->m_vRelevantTo, pNetworkPlayer);
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
        pNetworkPlayer->m_bHasPosition = true;

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

    CRelevanceManager::EnsureRelevant(pNetworkVehicle, pNewSyncer);
    pNetworkVehicle->m_pSyncer = pNewSyncer;

    // npc peds inside go together with the vehicle
    for (int i = 0; i < pHandoff->occupantCount; i++)
    {
        CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pHandoff->occupants[i].pedid);
        if (pNetworkPed && pNetworkPed->m_pSyncer == pNetworkPlayer)
        {
            CRelevanceManager::EnsureRelevant(pNetworkPed, pNewSyncer);
            pNetworkPed->m_pSyncer = pNewSyncer;
            pNetworkPed->m_pPendingSyncer = nullptr;
        }
    }

    // the client plays packets in serverTime order (CPacketBuffer): forwarded with the old syncer's time it would come
    // before the spawn EnsureRelevant has just sent, the new syncer would find nothing to take over and keep a clone
    pHandoff->serverTime = g_serverTime;
    GetPacketFactory().Send(*pHandoff, pNewSyncer);
}
