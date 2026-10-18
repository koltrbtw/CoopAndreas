#include "stdafx.h"
#include "CSectorManager.h"
#include <network/packets/world.h>

std::vector<uint32_t> CSectorManager::GetWantedCells(CNetworkPlayer* pNetworkPlayer)
{
    std::vector<uint32_t> cells;
    if (!pNetworkPlayer->m_bHasPosition)
    {
        return cells;
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
            cells.push_back(GetCell(pNetworkPlayer->m_nArea, WorldGrid::GetSector(x, y)));
        }
    }
    return cells;
}

void CSectorManager::Free(uint32_t cell)
{
    ms_owners.erase(cell);
    ms_freeSince[cell] = static_cast<uint32_t>(g_serverTime);
}

void CSectorManager::SendSectors(CNetworkPlayer* pNetworkPlayer)
{
    // the player's cells are all in its area: it has let go of the others
    Packets::World::WorldGridSectors packet{};
    packet.area = pNetworkPlayer->m_nArea;
    for (const auto& [cell, pOwner] : ms_owners)
    {
        if (pOwner == pNetworkPlayer && packet.count < WorldGrid::MAX_SECTORS_PER_PLAYER)
        {
            packet.sectors[packet.count++] = GetSectorOfCell(cell);
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
        std::vector<uint32_t> wanted = GetWantedCells(pNetworkPlayer);
        bool bChanged = false;

        // let go of the cells the player has moved away from (or left the area of)
        std::vector<uint32_t> left;
        for (const auto& [cell, pOwner] : ms_owners)
        {
            if (pOwner == pNetworkPlayer && std::find(wanted.begin(), wanted.end(), cell) == wanted.end())
            {
                left.push_back(cell);
            }
        }
        for (uint32_t cell : left)
        {
            Free(cell);
            bChanged = true;
        }

        // first come, first served
        for (uint32_t cell : wanted)
        {
            auto itFree = ms_freeSince.find(cell);
            if (ms_owners.count(cell) == 0 &&
                (itFree == ms_freeSince.end() || g_serverTime - itFree->second >= FREE_COOLDOWN))
            {
                ms_owners[cell] = pNetworkPlayer;
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
    std::vector<uint32_t> left;
    for (const auto& [cell, pOwner] : ms_owners)
    {
        if (pOwner == pNetworkPlayer)
        {
            left.push_back(cell);
        }
    }
    for (uint32_t cell : left)
    {
        Free(cell);
    }
}
