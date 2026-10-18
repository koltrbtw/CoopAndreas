#pragma once

#include <unordered_map>
#include <vector>
#include <network/world_grid.h>

class CNetworkPlayer;

// who spawns the ambient population where: a sector belongs to the first player that claimed it (the square of
// WorldGrid::OWNERSHIP_RADIUS around it) until that player moves away, so two players never spawn into the same
// place twice. The approach of the GTA V multiplayer the zone streaming is modelled on. The interiors lie over the
// map and over each other: every area (CGame::currArea) has a grid of its own, a cell is the area and the sector
class CSectorManager
{
public:
    static void Update();
    static void OnPlayerDisconnected(CNetworkPlayer* pNetworkPlayer);

private:
    static inline std::unordered_map<uint32_t, CNetworkPlayer*> ms_owners;
    static inline std::unordered_map<uint32_t, uint32_t> ms_freeSince;

    static uint32_t GetCell(uint8_t area, uint16_t sector) { return area * WorldGrid::SECTOR_COUNT + sector; }
    static uint16_t GetSectorOfCell(uint32_t cell) { return static_cast<uint16_t>(cell % WorldGrid::SECTOR_COUNT); }
    static std::vector<uint32_t> GetWantedCells(CNetworkPlayer* pNetworkPlayer);
    static void Free(uint32_t cell);
    static void SendSectors(CNetworkPlayer* pNetworkPlayer);
};
