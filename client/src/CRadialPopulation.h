#pragma once

// the ambient population around the player by the radius, not by where the camera looks: the vanilla code spawns
// visible things far and hidden ones right behind the player, and removes what is behind the camera much earlier.
// The spawn rings are set in the CWorldGridSync generator hooks
class CRadialPopulation
{
public:
    static void InjectHooks();
};
