#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// the map split into sectors: only the player owning a sector spawns ambient peds and vehicles in it, the others
// get them as clones (zone streaming, see the server CSectorManager)
namespace WorldGrid
{
constexpr float SECTOR_SIZE = 100.0f;
constexpr float ORIGIN = -3000.0f;
constexpr int SECTORS_PER_SIDE = 60;  // 6000 units of map
constexpr int SECTOR_COUNT = SECTORS_PER_SIDE * SECTORS_PER_SIDE;
constexpr float OWNERSHIP_RADIUS = 200.0f;  // a player claims the sectors of the square around it
constexpr int MAX_SECTORS_PER_PLAYER = 32;  // (2 * 200 / 100 + 1)^2 = 25

inline int GetAxis(float coordinate)
{
    return std::clamp(static_cast<int>(std::floor((coordinate - ORIGIN) / SECTOR_SIZE)), 0, SECTORS_PER_SIDE - 1);
}

inline uint16_t GetSector(int x, int y)
{
    return static_cast<uint16_t>(y * SECTORS_PER_SIDE + x);
}

inline uint16_t GetSector(float x, float y)
{
    return GetSector(GetAxis(x), GetAxis(y));
}
}  // namespace WorldGrid
