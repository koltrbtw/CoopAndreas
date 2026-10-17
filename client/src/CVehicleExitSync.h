#pragma once

// the local player getting out of a network vehicle. Only the leave task the exit key starts went out as an event;
// knocked off a bike, rolled out, dragged out, fell out dead, warped out by a script - the other players saw the
// clone pop out standing at the first on foot update, and the server kept the seat taken. Here the owner compares,
// every frame, the vehicle it last sat in with the game's one: whatever got it out, VEHICLE_EXIT says how
class CVehicleExitSync
{
public:
    static void Process();
    // every frame: the exits of remote players that waited for our own leave / jack of their clone
    static void ProcessClones();
    static void Reset();
    // CTaskComplexLeaveCar of the local player started (the exit key)
    static void SendLeave(CVehicle* pVehicle);
    // the clone side of VEHICLE_EXIT
    static void HandlePacket(const Packets::Vehicles::VehicleExit& packet);
    // after the vehicle let the clone go: the fall the sender plays
    static void ApplyFall(CPed* pPed, const Packets::Vehicles::VehicleExit& packet);
    // the exit that waited (CNetworkPlayer::m_pendingExit) now; false when there is none
    static bool ApplyPendingExit(CNetworkPlayer* pNetworkPlayer);

private:
    static void SendOut(CPlayerPed* pPlayerPed, vehicleid_t vehicleid);

    static inline vehicleid_t ms_vehicleId{};  // the network vehicle the local player sat in last frame
    static inline bool ms_bLeaveSent = false;  // VEHICLE_EXIT_LEAVE went for it
};
