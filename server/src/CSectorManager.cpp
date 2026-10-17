#include "stdafx.h"
#include "CSectorManager.h"
#include <network/packets/world.h>

std::vector<uint16_t> CSectorManager::GetWantedSectors(CNetworkPlayer* pNetworkPlayer)
{
    std::vector<uint16_t> sectors;
    if (!pNetworkPlayer->m_bHasPosition)
    {
        return sectors;
    }

    const CVector& position = pNetworkPlayer->m_vecPosition;
    int minX = WorldGrid::GetAxis(position.x - WorldGrid::OWNERSHIP_RADIUS);
    int maxX = WorldGrid::GetAxis(position.x + WorldGrid::OWNERSHIP_RADIUS);
    int minY = WorldGrid::GetAxis(position.y - WorldGrid::OWNERSHIP_RADIUS);
    int maxY = WorldGrid::GetAxis(position.y + WorldGrid::OWNERSHIP_RADIUS);

    for (int y = minY; y <= maxY; y++)
    {
        for (int x = minX; x <= maxX; x++)
        {
            sectors.push_back(WorldGrid::GetSector(x, y));
        }
    }
    return sectors;
}

void CSectorManager::SendSectors(CNetworkPlayer* pNetworkPlayer)
{
    Packets::World::WorldGridSectors packet{};
    for (int i = 0; i < WorldGrid::SECTOR_COUNT && packet.count < WorldGrid::MAX_SECTORS_PER_PLAYER; i++)
    {
        if (ms_apOwners[i] == pNetworkPlayer)
        {
            packet.sectors[packet.count++] = static_cast<uint16_t>(i);
        }
    }
    GetPacketFactory().Send(packet, pNetworkPlayer);
}

void CSectorManager::Update()
{
    static server_time_t nLastUpdate = 0;
    constexpr server_time_t UPDATE_INTERVAL = 250;
    constexpr server_time_t FREE_COOLDOWN = 100;  // a freed sector waits a bit, its old owner may still spawn there

    if (g_serverTime - nLastUpdate < UPDATE_INTERVAL)
    {
        return;
    }
    nLastUpdate = g_serverTime;

    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        std::vector<uint16_t> wanted = GetWantedSectors(pNetworkPlayer);
        bool bChanged = false;

        // let go of the sectors the player has moved away from
        for (int i = 0; i < WorldGrid::SECTOR_COUNT; i++)
        {
            if (ms_apOwners[i] == pNetworkPlayer && std::find(wanted.begin(), wanted.end(), i) == wanted.end())
            {
                ms_apOwners[i] = nullptr;
                ms_anFreeSince[i] = static_cast<uint32_t>(g_serverTime);
                bChanged = true;
            }
        }

        // first come, first served
        for (uint16_t sector : wanted)
        {
            if (!ms_apOwners[sector] && g_serverTime - ms_anFreeSince[sector] >= FREE_COOLDOWN)
            {
                ms_apOwners[sector] = pNetworkPlayer;
                bChanged = true;
            }
        }

        if (bChanged)
        {
            SendSectors(pNetworkPlayer);
        }
    }
}

void CSectorManager::OnPlayerDisconnected(CNetworkPlayer* pNetworkPlayer)
{
    for (int i = 0; i < WorldGrid::SECTOR_COUNT; i++)
    {
        if (ms_apOwners[i] == pNetworkPlayer)
        {
            ms_apOwners[i] = nullptr;
            ms_anFreeSince[i] = static_cast<uint32_t>(g_serverTime);
        }
    }
}
