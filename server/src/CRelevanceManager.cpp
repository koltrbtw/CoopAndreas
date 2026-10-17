#include "stdafx.h"
#include "CRelevanceManager.h"
#include "CNetworkPed.h"
#include "CNetworkVehicle.h"
#include <network/packets/peds.h>
#include <network/packets/vehicles.h>
#include <cmath>

// the server doesn't link the plugin-sdk CVector methods
static float GetDistance(const CVector& a, const CVector& b)
{
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

static bool Contains(const std::vector<CNetworkPlayer*>& players, CNetworkPlayer* pNetworkPlayer)
{
    return std::find(players.begin(), players.end(), pNetworkPlayer) != players.end();
}

static void Erase(std::vector<CNetworkPlayer*>& players, CNetworkPlayer* pNetworkPlayer)
{
    players.erase(std::remove(players.begin(), players.end(), pNetworkPlayer), players.end());
}

static bool IsAlwaysRelevant(CNetworkPed* pNetworkPed)
{
    return pNetworkPed->m_nCreatedBy == MISSION_CHAR;
}

static bool IsAlwaysRelevant(CNetworkVehicle* pNetworkVehicle)
{
    return pNetworkVehicle->m_nCreatedBy == MISSION_VEHICLE;
}

static bool WantsEntity(CNetworkPlayer* pNetworkPlayer, const CVector& position, bool bAlwaysRelevant, bool bHasIt)
{
    if (bAlwaysRelevant)
    {
        return true;
    }

    if (!pNetworkPlayer->m_bHasPosition)
    {
        return false;
    }

    float fDistance = GetDistance(pNetworkPlayer->m_vecPosition, position);
    return fDistance < (bHasIt ? CRelevanceManager::SCOPE_LEAVE_DISTANCE : CRelevanceManager::SCOPE_ENTER_DISTANCE);
}

void CRelevanceManager::SendPedSpawn(CNetworkPed* pNetworkPed, CNetworkPlayer* pNetworkPlayer)
{
    Packets::Peds::PedSpawn packet{};
    packet.pedid = pNetworkPed->m_pedId;
    packet.modelId = pNetworkPed->m_nModelId;
    packet.pos = pNetworkPed->m_vecPos;
    packet.pedType = pNetworkPed->m_nPedType;
    packet.createdBy = pNetworkPed->m_nCreatedBy;
    snprintf(packet.specialModelName, sizeof(packet.specialModelName), "%s", pNetworkPed->m_szSpecialModelName);
    GetPacketFactory().Send(packet, pNetworkPlayer);
}

void CRelevanceManager::SendVehicleSpawn(CNetworkVehicle* pNetworkVehicle, CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkVehicle->m_bTrain)
    {
        Packets::Vehicles::TrainSpawn trainSpawnPacket{};
        trainSpawnPacket.vehicleid = pNetworkVehicle->m_vehicleId;
        trainSpawnPacket.pos = pNetworkVehicle->m_vecPosition;
        trainSpawnPacket.isMission = pNetworkVehicle->m_nCreatedBy == MISSION_VEHICLE;
        trainSpawnPacket.state = pNetworkVehicle->m_trainState;
        GetPacketFactory().Send(trainSpawnPacket, pNetworkPlayer);
        return;
    }

    Packets::Vehicles::VehicleSpawn vehicleSpawnPacket{};
    vehicleSpawnPacket.vehicleid = pNetworkVehicle->m_vehicleId;
    vehicleSpawnPacket.modelid = pNetworkVehicle->m_nModelId;
    vehicleSpawnPacket.pos = pNetworkVehicle->m_vecPosition;
    vehicleSpawnPacket.rot = pNetworkVehicle->GetHeading();
    vehicleSpawnPacket.color1 = pNetworkVehicle->m_nPrimaryColor;
    vehicleSpawnPacket.color2 = pNetworkVehicle->m_nSecondaryColor;
    vehicleSpawnPacket.createdBy = static_cast<eVehicleCreatedBy>(pNetworkVehicle->m_nCreatedBy);
    vehicleSpawnPacket.extras[0] = pNetworkVehicle->m_anExtras[0];
    vehicleSpawnPacket.extras[1] = pNetworkVehicle->m_anExtras[1];
    GetPacketFactory().Send(vehicleSpawnPacket, pNetworkPlayer);

    Packets::Vehicles::VehicleDamage vehicleDamagePacket{};
    vehicleDamagePacket.vehicleid = pNetworkVehicle->m_vehicleId;
    vehicleDamagePacket.damageManager = pNetworkVehicle->m_damageManager;
    memcpy(vehicleDamagePacket.bikeWheelStatus, pNetworkVehicle->m_aBikeWheelStatus, sizeof(pNetworkVehicle->m_aBikeWheelStatus));
    GetPacketFactory().Send(vehicleDamagePacket, pNetworkPlayer);

    for (int component : pNetworkVehicle->m_pComponents)
    {
        Packets::Vehicles::VehicleComponentAdd vehicleComponentAdd{};
        vehicleComponentAdd.vehicleid = pNetworkVehicle->m_vehicleId;
        vehicleComponentAdd.componentid = component;
        GetPacketFactory().Send(vehicleComponentAdd, pNetworkPlayer);
    }
}

void CRelevanceManager::OnPedCreated(CNetworkPed* pNetworkPed)
{
    pNetworkPed->m_vRelevantTo = {pNetworkPed->m_pSyncer};

    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer != pNetworkPed->m_pSyncer &&
            WantsEntity(pNetworkPlayer, pNetworkPed->m_vecPos, IsAlwaysRelevant(pNetworkPed), false))
        {
            pNetworkPed->m_vRelevantTo.push_back(pNetworkPlayer);
            SendPedSpawn(pNetworkPed, pNetworkPlayer);
        }
    }
}

void CRelevanceManager::OnVehicleCreated(CNetworkVehicle* pNetworkVehicle)
{
    pNetworkVehicle->m_vRelevantTo = {pNetworkVehicle->m_pSyncer};

    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer != pNetworkVehicle->m_pSyncer &&
            WantsEntity(pNetworkPlayer, pNetworkVehicle->m_vecPosition, IsAlwaysRelevant(pNetworkVehicle), false))
        {
            pNetworkVehicle->m_vRelevantTo.push_back(pNetworkPlayer);
            SendVehicleSpawn(pNetworkVehicle, pNetworkPlayer);
        }
    }
}

void CRelevanceManager::EnsureRelevant(CNetworkPed* pNetworkPed, CNetworkPlayer* pNetworkPlayer)
{
    if (!Contains(pNetworkPed->m_vRelevantTo, pNetworkPlayer))
    {
        pNetworkPed->m_vRelevantTo.push_back(pNetworkPlayer);
        SendPedSpawn(pNetworkPed, pNetworkPlayer);
    }
}

void CRelevanceManager::EnsureRelevant(CNetworkVehicle* pNetworkVehicle, CNetworkPlayer* pNetworkPlayer)
{
    if (!Contains(pNetworkVehicle->m_vRelevantTo, pNetworkPlayer))
    {
        pNetworkVehicle->m_vRelevantTo.push_back(pNetworkPlayer);
        SendVehicleSpawn(pNetworkVehicle, pNetworkPlayer);
    }
}

void CRelevanceManager::OnCloneDropped(CNetworkVehicle* pNetworkVehicle, CNetworkPlayer* pNetworkPlayer)
{
    Erase(pNetworkVehicle->m_vRelevantTo, pNetworkPlayer);
}

void CRelevanceManager::SendToRelevant(Packet& packet, const std::vector<CNetworkPlayer*>& relevantTo, CNetworkPlayer* pExcept)
{
    for (auto pNetworkPlayer : relevantTo)
    {
        if (pNetworkPlayer != pExcept)
        {
            GetPacketFactory().Send(packet, pNetworkPlayer);
        }
    }
}

void CRelevanceManager::OnPlayerDisconnected(CNetworkPlayer* pNetworkPlayer)
{
    for (auto pNetworkPed : CNetworkPedManager::m_pPeds)
    {
        Erase(pNetworkPed->m_vRelevantTo, pNetworkPlayer);
    }

    for (auto pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        Erase(pNetworkVehicle->m_vRelevantTo, pNetworkPlayer);
    }
}

void CRelevanceManager::RemoveForgottenVehicles()
{
    // its syncer lost it without VEHICLE_REMOVE: the clones would stay with everyone, and a parked car generator
    // that made it again would pile them up at its spot
    constexpr uint32_t FORGOTTEN_TIME = 15000;

    for (auto pNetworkVehicle : std::vector<CNetworkVehicle*>(CNetworkVehicleManager::m_pVehicles))
    {
        // a standing train sends nothing
        if (pNetworkVehicle->m_bTrain || g_serverTime - pNetworkVehicle->m_nLastSyncerPacketTime < FORGOTTEN_TIME)
        {
            continue;
        }

        logger::warn("vehicle %s: nothing from its syncer %s for 15 s, removed", to_string(pNetworkVehicle->m_vehicleId).c_str(),
            pNetworkVehicle->m_pSyncer ? pNetworkVehicle->m_pSyncer->GetName().c_str() : "-");

        // the syncer too: if it does have it after all, its copy goes away with the others
        Packets::Vehicles::VehicleRemove packet{};
        packet.vehicleid = pNetworkVehicle->m_vehicleId;
        SendToRelevant(packet, pNetworkVehicle->m_vRelevantTo, nullptr);

        CNetworkVehicleManager::Remove(pNetworkVehicle);
    }
}

void CRelevanceManager::Update()
{
    static server_time_t nLastUpdate = 0;
    constexpr server_time_t UPDATE_INTERVAL = 500;

    if (g_serverTime - nLastUpdate < UPDATE_INTERVAL)
    {
        return;
    }

    nLastUpdate = g_serverTime;

    RemoveForgottenVehicles();

    // removals first and peds before vehicles: a clone ped must not stay in a removed clone vehicle
    for (auto pNetworkPed : CNetworkPedManager::m_pPeds)
    {
        for (auto pNetworkPlayer : std::vector<CNetworkPlayer*>(pNetworkPed->m_vRelevantTo))
        {
            if (pNetworkPlayer == pNetworkPed->m_pSyncer ||
                WantsEntity(pNetworkPlayer, pNetworkPed->m_vecPos, IsAlwaysRelevant(pNetworkPed), true))
            {
                continue;
            }

            Erase(pNetworkPed->m_vRelevantTo, pNetworkPlayer);

            Packets::Peds::PedRemove packet{};
            packet.pedid = pNetworkPed->m_pedId;
            GetPacketFactory().Send(packet, pNetworkPlayer);
        }
    }

    for (auto pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        for (auto pNetworkPlayer : std::vector<CNetworkPlayer*>(pNetworkVehicle->m_vRelevantTo))
        {
            if (pNetworkPlayer == pNetworkVehicle->m_pSyncer ||
                WantsEntity(pNetworkPlayer, pNetworkVehicle->m_vecPosition, IsAlwaysRelevant(pNetworkVehicle), true))
            {
                continue;
            }

            Erase(pNetworkVehicle->m_vRelevantTo, pNetworkPlayer);

            Packets::Vehicles::VehicleRemove packet{};
            packet.vehicleid = pNetworkVehicle->m_vehicleId;
            GetPacketFactory().Send(packet, pNetworkPlayer);
        }
    }

    // then additions, vehicles before peds: a clone driver is put into its vehicle by the next driver update
    for (auto pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
        {
            if (!Contains(pNetworkVehicle->m_vRelevantTo, pNetworkPlayer) &&
                WantsEntity(pNetworkPlayer, pNetworkVehicle->m_vecPosition, IsAlwaysRelevant(pNetworkVehicle), false))
            {
                pNetworkVehicle->m_vRelevantTo.push_back(pNetworkPlayer);
                SendVehicleSpawn(pNetworkVehicle, pNetworkPlayer);
            }
        }
    }

    for (auto pNetworkPed : CNetworkPedManager::m_pPeds)
    {
        for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
        {
            if (!Contains(pNetworkPed->m_vRelevantTo, pNetworkPlayer) &&
                WantsEntity(pNetworkPlayer, pNetworkPed->m_vecPos, IsAlwaysRelevant(pNetworkPed), false))
            {
                pNetworkPed->m_vRelevantTo.push_back(pNetworkPlayer);
                SendPedSpawn(pNetworkPed, pNetworkPlayer);
            }
        }
    }
}
