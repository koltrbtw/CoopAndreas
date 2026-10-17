#pragma once

// the population code removes ambient peds and vehicles by the distance to the local player only, even when another
// player stands next to them. The ones we sync go to the nearest such player instead of disappearing in front of him
class CPopulationHandover
{
public:
    // a bit beyond the far border of CPopulation::ManagePed (54.5..65 m)
    static constexpr float PED_HANDOVER_DISTANCE = 70.0f;
    // the full removal range of CCarCtrl::PossiblyRemoveVehicle
    static constexpr float VEHICLE_HANDOVER_DISTANCE = 170.0f;

    static CNetworkPlayer* FindNearestRemotePlayer(const CVector& position, float fMaxDistance);
    static bool TryHandOverPed(CPed* pPed);
    static bool TryHandOverVehicle(CVehicle* pVehicle);

    static void InjectHooks();
};
