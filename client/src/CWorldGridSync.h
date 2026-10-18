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
    // the peds an interior gets on the way in: nobody else is in it
    static bool CanPopulateInterior(const CVector& position);

    static void InjectHooks();

    static inline bool ms_bPopulatingInterior = false;  // inside CPopulation::PopulateInterior we let through

private:
    static constexpr float INTERIOR_RADIUS = 150.0f;
    static inline std::bitset<WorldGrid::SECTOR_COUNT> ms_ownedSectors{};
    static inline uint8_t ms_nOwnedArea = 0;  // the area (interior) of the sectors
};
