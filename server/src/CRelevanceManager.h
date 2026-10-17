#pragma once

#include <vector>

class CNetworkPed;
class CNetworkVehicle;
class CNetworkPlayer;
class Packet;

// which player has a clone of which ped / vehicle. Ambient entities exist only for the players near them (spawned
// when they come in scope, removed when they leave it), mission ones and the syncer's own are always there
class CRelevanceManager
{
public:
    static constexpr float SCOPE_ENTER_DISTANCE = 300.0f;
    static constexpr float SCOPE_LEAVE_DISTANCE = 350.0f;  // a bit further, so a player on the border doesn't flicker

    static void Update();

    // the creator has it already, the others in scope get the spawn
    static void OnPedCreated(CNetworkPed* pNetworkPed);
    static void OnVehicleCreated(CNetworkVehicle* pNetworkVehicle);

    // before a player becomes the syncer: it must have the clone
    static void EnsureRelevant(CNetworkPed* pNetworkPed, CNetworkPlayer* pNetworkPlayer);
    static void EnsureRelevant(CNetworkVehicle* pNetworkVehicle, CNetworkPlayer* pNetworkPlayer);
    // the player dropped its clone, see VEHICLE_REMOVE from a non-syncer
    static void RemoveForgottenVehicles();
    static void OnCloneDropped(CNetworkVehicle* pNetworkVehicle, CNetworkPlayer* pNetworkPlayer);

    static void SendToRelevant(Packet& packet, const std::vector<CNetworkPlayer*>& relevantTo, CNetworkPlayer* pExcept);

    static void OnPlayerDisconnected(CNetworkPlayer* pNetworkPlayer);

    static void SendPedSpawn(CNetworkPed* pNetworkPed, CNetworkPlayer* pNetworkPlayer);
    static void SendVehicleSpawn(CNetworkVehicle* pNetworkVehicle, CNetworkPlayer* pNetworkPlayer);
};
