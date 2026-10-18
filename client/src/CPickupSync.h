#pragma once

// the pickups every game has of its own (INITIAL, MAIN, the IPL ones; on street and once): the server lets one
// player take one (PICKUP_COLLECT_REQUEST, PICKUP_GRANT), the others take it away for as long as the collector's
// game does (PICKUP_STATE). Every game used to give the same pickup to its own player, the money twice
class CPickupSync
{
public:
    static void InjectHooks();
    static void Process();
    static void HandleGrant(const Packets::Pickups::PickupGrant& packet);
    static void HandleState(const Packets::Pickups::PickupState& packet);
    static void Reset();
};
