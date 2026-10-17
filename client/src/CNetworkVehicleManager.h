#pragma once
class CNetworkVehicleManager
{
public:
    static inline std::vector<CNetworkVehicle*> m_pVehicles;

    static inline uint32_t ms_nNextVehicleId = 1;

    static CNetworkVehicle* GetVehicle(vehicleid_t vehicleid);
    static CNetworkVehicle* GetVehicle(CEntity* vehicle);
    static void Add(CNetworkVehicle* vehicle);
    static void Remove(CNetworkVehicle* vehicle);
    static void UpdateDriver(CVehicle* pVehicle);
    static void UpdateIdle();
    // every frame before the vehicle's ProcessControl: a car woken up from hanging in the air keeps its physics
    static void KeepAwake(CVehicle* pVehicle);
    // every frame after the vehicle's ProcessControl: a clone moves to the synced transform, keeps its rotor
    static void AfterProcessControl(CVehicle* pVehicle);
    // every frame
    static void Process();
    static void UpdatePassenger(CVehicle* pVehicle, CPlayerPed* pPlayerPed);
    static vehicleid_t AllocateVehicleId();
    static void ResetIdAllocator();
    static void RemoveHostedUnused();
    // clones whose syncer forgot them: nothing from it for a while, or a silent one where a new clone of the same model of
    // the same creator comes (a parked car generator that made it again). Dropped here and taken off our scope on
    // the server, which spawns it again if it does exist
    static void DropStaleClones();
    static void DropClone(CNetworkVehicle* pNetworkVehicle, const char* szReason);
    static CNetworkVehicle* FindSameClone(vehicleid_t creatorOf, int modelId, const CVector& position);
    static void UpdateDamageSync();
};

