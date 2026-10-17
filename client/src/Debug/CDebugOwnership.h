#pragma once

// debug instances only (--coopd*): F5 asks the server for the nearest someone else's npc driven vehicle (any vehicle if
// there is none), F6 for the nearest on foot ped, to try the ownership handoff before the streaming rules trigger it
class CDebugOwnership
{
public:
    static void Process();
    static void WatchVehicle(vehicleid_t vehicleid);
};
