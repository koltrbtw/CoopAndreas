#include "network/packets/peds.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "CRelevanceManager.h"

PACKET_HANDLER(ePacketType::PED_SPAWN, Packets::Peds::PedSpawn* pPedSpawn, CNetworkPlayer* pNetworkPlayer)
{
    static char allowedSpecialActors[52][8] = {"ANDRE", "BBTHIN", "BB", "CAT", "CESAR", "COPGRL1", "COPGRL2", "COPGRL3",
        "CLAUDE", "CROGRL1", "CROGRL2", "CROGRL3", "DWAYNE", "EMMET", "FORELLI", "GANGRL1", "GANGRL2", "GANGRL3",
        "GUNGRL1", "GUNGRL2", "GUNGRL3", "HERN", "JANITOR", "JETHRO", "JIZZY", "KENDL", "MACCER", "MADDOGG", "MECGRL1",
        "MECGRL2", "MECGRL3", "NURGRL1", "NURGRL2", "NURGRL3", "OGLOC", "PAUL", "PULASKI", "ROSE", "RYDER1", "RYDER2",
        "RYDER3", "SINDACO", "SMOKE", "SMOKEV", "SUZIE", "SWEET", "TBONE", "TENPEN", "TORINO", "TRUTH", "WUZIMU",
        "ZERO"};

    if (pPedSpawn->modelId >= MODEL_SPECIAL01 && pPedSpawn->modelId <= MODEL_SPECIAL10)
    {
        bool isSpecialModelValid = false;

        for (int i = 0; i < ARRAY_SIZE(allowedSpecialActors); i++)
        {
            if (strncasecmp(pPedSpawn->specialModelName, allowedSpecialActors[i], strlen(allowedSpecialActors[i])))
            {
                isSpecialModelValid = true;
                break;
            }
        }

        if (!isSpecialModelValid)
            return;
    }

    // the id is allocated by its creator, a player can only create peds in its own id space
    if (pPedSpawn->pedid.creator() != pNetworkPlayer->m_playerId)
    {
        logger::warn("%s tried to create a ped with someone else's id %s", pNetworkPlayer->GetName().c_str(),
            to_string(pPedSpawn->pedid).c_str());
        return;
    }

    if (CNetworkPedManager::GetPed(pPedSpawn->pedid) != nullptr)
    {
        logger::warn("%s tried to create a ped with an id that is already in use %s",
            pNetworkPlayer->GetName().c_str(), to_string(pPedSpawn->pedid).c_str());
        return;
    }

    CNetworkPed* pNetworkPed = new CNetworkPed(
        pPedSpawn->pedid, pNetworkPlayer, pPedSpawn->modelId, pPedSpawn->pedType, pPedSpawn->pos, pPedSpawn->createdBy);
    snprintf(pNetworkPed->m_szSpecialModelName, sizeof(pNetworkPed->m_szSpecialModelName), "%s",
        pPedSpawn->specialModelName);

    CNetworkPedManager::Add(pNetworkPed);
    CRelevanceManager::OnPedCreated(pNetworkPed);
}

PACKET_HANDLER(ePacketType::PED_REMOVE, Packets::Peds::PedRemove* pPedRemove, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedRemove->pedid);

    if (!pNetworkPed)
    {
        return;
    }

    if (pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        logger::warn("%s tries to remove someone else's ped", pNetworkPlayer->GetName().c_str());
        return;
    }

    for (auto p : CNetworkPlayerManager::m_pPlayers)
    {
        // check if this player has claimed the ped
        if (std::find(p->m_vPedClaims.begin(), p->m_vPedClaims.end(), pNetworkPed) != p->m_vPedClaims.end())
        {
            // assign ped's syncer to this player
            CRelevanceManager::EnsureRelevant(pNetworkPed, p);
            pNetworkPed->m_pSyncer = p;
            pNetworkPed->m_pPendingSyncer = nullptr;

            // send ASSIGN_PED packet to the new host player to notify them they now own the ped
            Packets::Peds::AssignPedSyncer assignPedPacket{};
            assignPedPacket.pedid = pNetworkPed->m_pedId;
            assignPedPacket.isSyncer = true;
            GetPacketFactory().Send(assignPedPacket, p);

            // send PED_SPAWN packet to the old syncer (peer) so it can respawn the ped locally
            Packets::Peds::PedSpawn pedSpawnPacket{};
            pedSpawnPacket.pedid = pNetworkPed->m_pedId;
            pedSpawnPacket.modelId = pNetworkPed->m_nModelId;
            pedSpawnPacket.pedType = pNetworkPed->m_nPedType;
            pedSpawnPacket.pos = pNetworkPed->m_vecPos;
            snprintf(pedSpawnPacket.specialModelName, sizeof(pedSpawnPacket.specialModelName), "%s",
                pNetworkPed->m_szSpecialModelName);
            pedSpawnPacket.createdBy = pNetworkPed->m_nCreatedBy;
            GetPacketFactory().Send(pedSpawnPacket, pNetworkPlayer);

            // remove the ped from this player's claim list to avoid duplicate claims
            auto it = std::find(p->m_vPedClaims.begin(), p->m_vPedClaims.end(), pNetworkPed);
            if (it != p->m_vPedClaims.end())
            {
                p->m_vPedClaims.erase(it);
            }

            // exit after assigning new host and respawning ped
            return;
        }
    }

    // if nobody claimed the ped, remove it from all players' claim lists
    for (auto p : CNetworkPlayerManager::m_pPlayers)
    {
        auto it = std::find(p->m_vPedClaims.begin(), p->m_vPedClaims.end(), pNetworkPed);
        if (it != p->m_vPedClaims.end())
        {
            p->m_vPedClaims.erase(it);
        }
    }

    // the server time: with the syncer's one it may come before the spawn the server sent (CPacketBuffer plays by
    // serverTime), the others would keep the clone
    pPedRemove->serverTime = g_serverTime;
    CRelevanceManager::SendToRelevant(*pPedRemove, pNetworkPed->m_vRelevantTo, pNetworkPlayer);
    CNetworkPedManager::Remove(pNetworkPed);
}

PACKET_HANDLER(ePacketType::PED_ONFOOT, Packets::Peds::PedOnFoot* pPedOnFoot, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pPed = CNetworkPedManager::GetPed(pPedOnFoot->pedid);
    if (pPed)
    {
        if (pPed->m_pSyncer != pNetworkPlayer)
        {
            logger::warn("%s tries to update (on foot) someone else's ped", pNetworkPlayer->GetName().c_str());
            return;
        }

        pPed->m_vecPos = pPedOnFoot->pos;
        CRelevanceManager::SendToRelevant(*pPedOnFoot, pPed->m_vRelevantTo, pNetworkPlayer);
    }
}

PACKET_HANDLER(
    ePacketType::PED_DRIVER_UPDATE, Packets::Peds::PedDriverUpdate* pPedDriverUpdate, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedDriverUpdate->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    if (pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        logger::warn("%s tries to update (driver) someone else's ped", pNetworkPlayer->GetName().c_str());
        return;
    }

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedDriverUpdate->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    pNetworkVehicle->m_bUsedByPed = true;
    pNetworkVehicle->m_nLastSyncerPacketTime = g_serverTime;
    pNetworkVehicle->m_vecPosition = pPedDriverUpdate->pos;
    pNetworkVehicle->m_vecRotation = pPedDriverUpdate->rot;
    pNetworkVehicle->m_vecRight = pPedDriverUpdate->roll;
    pNetworkPed->m_vecPos = pPedDriverUpdate->pos;

    CRelevanceManager::SendToRelevant(*pPedDriverUpdate, pNetworkPed->m_vRelevantTo, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_PASSENGER_UPDATE, Packets::Peds::PedPassengerSync* pPedPassengerSync,
    CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedPassengerSync->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    if (pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        logger::warn("%s tries to update (passenger) someone else's ped", pNetworkPlayer->GetName().c_str());
        return;
    }

    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedPassengerSync->vehicleid))
    {
        pNetworkPed->m_vecPos = pNetworkVehicle->m_vecPosition;
    }

    CRelevanceManager::SendToRelevant(*pPedPassengerSync, pNetworkPed->m_vRelevantTo, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_SHOT_SYNC, Packets::Peds::PedShotSync* pPedShotSync, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedShotSync->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    if (pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        logger::warn("%s tries to update (shoot) someone else's ped", pNetworkPlayer->GetName().c_str());
        return;
    }

    CRelevanceManager::SendToRelevant(*pPedShotSync, pNetworkPed->m_vRelevantTo, pNetworkPlayer);
}

PACKET_HANDLER(
    ePacketType::PED_VEHICLE_ENTER, Packets::Peds::PedVehicleEnter* pPedVehicleEnter, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedVehicleEnter->pedid);
    if (pNetworkPed == nullptr || pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        return;
    }

    pPedVehicleEnter->senderPlayerid = pNetworkPlayer->m_playerId;
    CRelevanceManager::SendToRelevant(*pPedVehicleEnter, pNetworkPed->m_vRelevantTo, pNetworkPlayer);
}

PACKET_HANDLER(
    ePacketType::PED_VEHICLE_EXIT, Packets::Peds::PedVehicleExit* pPedVehicleExit, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedVehicleExit->pedid);
    if (pNetworkPed == nullptr || pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        return;
    }
    CRelevanceManager::SendToRelevant(*pPedVehicleExit, pNetworkPed->m_vRelevantTo, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_DIED, Packets::Peds::PedDied* pPedDied, CNetworkPlayer* pNetworkPlayer)
{
    if (pPedDied->entity.entityType == NETWORK_ENTITY_TYPE_PLAYER)
    {
        pPedDied->entity.playerid = pNetworkPlayer->m_playerId;
        GetPacketFactory().SendToAll(*pPedDied, pNetworkPlayer);
        return;
    }

    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedDied->entity.pedid);
    if (pNetworkPed == nullptr || pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        return;
    }
    CRelevanceManager::SendToRelevant(*pPedDied, pNetworkPed->m_vRelevantTo, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_STEALTH_KILL, Packets::Peds::PedStealthKill* pPedStealthKill, CNetworkPlayer* pNetworkPlayer)
{
    pPedStealthKill->senderPlayerid = pNetworkPlayer->m_playerId;
    GetPacketFactory().SendToAll(*pPedStealthKill, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_SAY, Packets::Peds::PedSay* pPedSay, CNetworkPlayer* pNetworkPlayer)
{
    if (pPedSay->entity.entityType == NETWORK_ENTITY_TYPE_PLAYER)
    {
        pPedSay->entity.playerid = pNetworkPlayer->m_playerId;
    }
    else if (pPedSay->entity.entityType == NETWORK_ENTITY_TYPE_PED)
    {
        CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedSay->entity.pedid);
        if (pNetworkPed == nullptr)
        {
            return;
        }
    }

    GetPacketFactory().SendToAll(*pPedSay, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_CLAIM_ON_RELEASE, Packets::Peds::PedClaimOnRelease* pPedClaimOnRelease,
    CNetworkPlayer* pNetworkPlayer)
{
    auto pNetworkPed = CNetworkPedManager::GetPed(pPedClaimOnRelease->pedid);

    if (pNetworkPed && pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        if (std::find(pNetworkPlayer->m_vPedClaims.begin(), pNetworkPlayer->m_vPedClaims.end(), pNetworkPed) ==
            pNetworkPlayer->m_vPedClaims.end())
        {
            pNetworkPlayer->m_vPedClaims.push_back(pNetworkPed);
        }
    }
}

PACKET_HANDLER(
    ePacketType::PED_CANCEL_CLAIM, Packets::Peds::PedCancelClaim* pPedCancelClaim, CNetworkPlayer* pNetworkPlayer)
{
    auto pNetworkPed = CNetworkPedManager::GetPed(pPedCancelClaim->pedid);

    if (pNetworkPed && pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        auto it = std::find(pNetworkPlayer->m_vPedClaims.begin(), pNetworkPlayer->m_vPedClaims.end(), pNetworkPed);
        if (it != pNetworkPlayer->m_vPedClaims.end())
        {
            pNetworkPlayer->m_vPedClaims.erase(it);
        }
    }
}

PACKET_HANDLER(ePacketType::PED_RESET_ALL_CLAIMS, Packets::Peds::PedResetAllClaims* pPedResetAllClaims,
    CNetworkPlayer* pNetworkPlayer)
{
    auto pNetworkPed = CNetworkPedManager::GetPed(pPedResetAllClaims->pedid);

    if (pNetworkPed)
    {
        if (pNetworkPed->m_pSyncer == pNetworkPlayer || pNetworkPlayer->m_bIsHost)
        {
            if (pNetworkPlayer->m_bIsHost && pNetworkPed->m_pSyncer != nullptr)
            {
                // here was a bug that i have not understood yet

                // the spawn first: one without the ped ignores the assignment
                CRelevanceManager::EnsureRelevant(pNetworkPed, pNetworkPlayer);

                Packets::Peds::AssignPedSyncer assignPedPacket{};
                assignPedPacket.pedid = pPedResetAllClaims->pedid;
                assignPedPacket.isSyncer = true;
                GetPacketFactory().Send(assignPedPacket, pNetworkPlayer);
                assignPedPacket.isSyncer = false;
                GetPacketFactory().Send(assignPedPacket, pNetworkPed->m_pSyncer);  // unassign old

                pNetworkPed->m_pSyncer = pNetworkPlayer;
                pNetworkPed->m_pPendingSyncer = nullptr;
            }

            for (auto p : CNetworkPlayerManager::m_pPlayers)
            {
                auto it = std::find(p->m_vPedClaims.begin(), p->m_vPedClaims.end(), pNetworkPed);
                if (it != p->m_vPedClaims.end())
                {
                    GetPacketFactory().Send(*pPedResetAllClaims, p);
                    p->m_vPedClaims.erase(it);
                }
            }
        }
    }
}

PACKET_HANDLER(ePacketType::PED_TAKE_HOST, Packets::Peds::PedTakeHost* pPedTakeHost, CNetworkPlayer* pNetworkPlayer)
{
    auto pNetworkPed = CNetworkPedManager::GetPed(pPedTakeHost->pedid);

    if (pNetworkPed)
    {
        if (pNetworkPed->m_pSyncer != pNetworkPlayer && pNetworkPlayer->m_bIsHost)
        {
            // the spawn first: one without the ped ignores the assignment
            CRelevanceManager::EnsureRelevant(pNetworkPed, pNetworkPlayer);

            Packets::Peds::AssignPedSyncer assignPedPacket{};
            assignPedPacket.pedid = pPedTakeHost->pedid;
            assignPedPacket.isSyncer = true;
            GetPacketFactory().Send(assignPedPacket, pNetworkPlayer);

            auto it = std::find(pNetworkPlayer->m_vPedClaims.begin(), pNetworkPlayer->m_vPedClaims.end(), pNetworkPed);
            if (it != pNetworkPlayer->m_vPedClaims.end())
            {
                pNetworkPlayer->m_vPedClaims.erase(it);
            }

            assignPedPacket.isSyncer = false;
            GetPacketFactory().Send(assignPedPacket, pNetworkPed->m_pSyncer);

            if (pPedTakeHost->allowReturnToPreviousHost)
            {
                if (std::find(pNetworkPed->m_pSyncer->m_vPedClaims.begin(), pNetworkPed->m_pSyncer->m_vPedClaims.end(), pNetworkPed) ==
                    pNetworkPed->m_pSyncer->m_vPedClaims.end())
                {
                    pNetworkPed->m_pSyncer->m_vPedClaims.push_back(pNetworkPed);
                }
            }

            pNetworkPed->m_pSyncer = pNetworkPlayer;
            pNetworkPed->m_pPendingSyncer = nullptr;
        }
    }
}

PACKET_HANDLER(ePacketType::PED_REQUEST_OWNERSHIP, Packets::Peds::PedRequestOwnership* pRequest, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pRequest->pedid);
    if (!pNetworkPed || !pNetworkPed->m_pSyncer || pNetworkPed->m_pSyncer == pNetworkPlayer ||
        pNetworkPed->m_pPendingSyncer)
    {
        return;
    }

    // mission peds belong to the scripts, see PED_CLAIM_ON_RELEASE and PED_TAKE_HOST for them
    if (pNetworkPed->m_nCreatedBy == MISSION_CHAR)
    {
        return;
    }

    pNetworkPed->m_pPendingSyncer = pNetworkPlayer;
    GetPacketFactory().Send(*pRequest, pNetworkPed->m_pSyncer);
}

PACKET_HANDLER(ePacketType::PED_HANDOFF, Packets::Peds::PedHandoff* pHandoff, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pHandoff->pedid);
    if (!pNetworkPed || pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        return;
    }

    CNetworkPlayer* pNewSyncer = pNetworkPed->m_pPendingSyncer;
    pNetworkPed->m_pPendingSyncer = nullptr;

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

    CRelevanceManager::EnsureRelevant(pNetworkPed, pNewSyncer);
    pNetworkPed->m_pSyncer = pNewSyncer;

    // the client plays packets in serverTime order (CPacketBuffer): forwarded with the old syncer's time it would come
    // before the spawn EnsureRelevant has just sent, the new syncer would find nothing to take over and keep a clone
    pHandoff->serverTime = g_serverTime;
    GetPacketFactory().Send(*pHandoff, pNewSyncer);
}
