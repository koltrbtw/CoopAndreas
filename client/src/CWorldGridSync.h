#pragma once

#include <bitset>
#include <network/world_grid.h>

// the sectors where we spawn the ambient population; everywhere else the owner of the sector does and we get its
// peds and vehicles as clones (server CSectorManager)
class CWorldGridSync
{
public:
    static void HandlePacket(Packets::World::WorldGridSectors* pPacket);
    static void Reset();

    static bool CanSpawnAt(const CVector& position);

    static void InjectHooks();

private:
    static inline std::bitset<WorldGrid::SECTOR_COUNT> ms_ownedSectors{};
};
