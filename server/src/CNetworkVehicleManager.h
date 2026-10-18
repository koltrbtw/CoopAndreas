#pragma once


class CNetworkVehicleManager
{
public:
    static std::vector<CNetworkVehicle*> m_pVehicles;
    static void Add(CNetworkVehicle* vehicle);
    static void Remove(CNetworkVehicle* vehicle);
    static CNetworkVehicle* GetVehicle(vehicleid_t vehicleid);
    static void RemoveAllHostedAndNotify(CNetworkPlayer* player);
    // a towed vehicle (a trailer, a car behind a tow truck) goes to the syncer of its tractor: its own syncer saw it
    // standing and its idle updates pulled it back from the tow bar
    static void AssignTowed(vehicleid_t trailerid, CNetworkPlayer* pTractorSyncer);
};