#pragma once

// the population code removes ambient peds and vehicles by the distance to the local player only, even when another
// player stands next to them. The ones we sync go to the nearest such player instead of disappearing in front of him
class CPopulationHandover
{
public:
    // the target's own population code must want to keep it, otherwise it removes it again and hands it back:
    // under the far border of CPopulation::ManagePed (54.5 m), well under the vehicle one for things behind the camera
    static constexpr float PED_HANDOVER_DISTANCE = 50.0f;
    static constexpr float VEHICLE_HANDOVER_DISTANCE = 120.0f;
    // received that recently and removed again: nobody needs it, remove instead of a ping-pong
    static constexpr unsigned int REMOVAL_HANDOVER_HOLD_TIME = 10000;

    static CNetworkPlayer* FindNearestRemotePlayer(const CVector& position, float fMaxDistance);
    static bool TryHandOverPed(CPed* pPed);
    static bool TryHandOverVehicle(CVehicle* pVehicle);
    static void HandOverPed(CNetworkPed* pNetworkPed, CNetworkPlayer* pTarget);
    static void HandOverVehicle(CNetworkVehicle* pNetworkVehicle, CNetworkPlayer* pTarget);

    // every 500 ms: our ambient entities go to a player clearly closer to them
    static void Process();

    static void InjectHooks();
};
