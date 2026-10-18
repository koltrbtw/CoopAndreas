#pragma once

// a vehicle blows up on its syncer only (CVehicle::BlowUpCar: its fire timer, a ram, a cheat, a script), it sends
// VEHICLE_BLOWN and its clones blow up then; a clone never by itself, its burning timer was one more explosion with
// its own damage on every game. The late ones get a wreck with no explosion (VehicleSpawn::wrecked)
class CVehicleBlowSync
{
public:
    static void InjectHooks();
    static void HandlePacket(const Packets::Vehicles::VehicleBlown& packet);
    // a clone that blew up before it came in our scope
    static void MakeWreck(CVehicle* pVehicle);
    // the game damages the car parts in BlowUpCar, the vehicle hooks let it then (they keep a clone's parts to its
    // syncer's damage packets)
    static bool IsFromNetwork() { return ms_bFromNetwork; }

private:
    static inline bool ms_bFromNetwork = false;
};
