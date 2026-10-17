#pragma once

#include <array>
#include <vector>
#include <network/world_grid.h>

class CNetworkPlayer;

// who spawns the ambient population where: a sector belongs to the first player that claimed it (the square of
// WorldGrid::OWNERSHIP_RADIUS around it) until that player moves away, so two players never spawn into the same
// place twice. The approach of the GTA V multiplayer the zone streaming is modelled on
class CSectorManager
{
public:
    static void Update();
    static void OnPlayerDisconnected(CNetworkPlayer* pNetworkPlayer);

private:
    static inline std::array<CNetworkPlayer*, WorldGrid::SECTOR_COUNT> ms_apOwners{};
    static inline std::array<uint32_t, WorldGrid::SECTOR_COUNT> ms_anFreeSince{};

    static std::vector<uint16_t> GetWantedSectors(CNetworkPlayer* pNetworkPlayer);
    static void SendSectors(CNetworkPlayer* pNetworkPlayer);
};
