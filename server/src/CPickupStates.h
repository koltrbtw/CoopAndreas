#pragma once

#include "network/packets/pickups.h"
#include <unordered_map>

class CNetworkPlayer;

// the judge of the pickups every game has of its own (see Packets::Pickups::PickupKey): one player gets one, the
// others take it away for as long as the collector's game does (an on street one regenerates, a once one never)
class CPickupStates
{
public:
    static void HandleRequest(const Packets::Pickups::PickupCollectRequest& packet, CNetworkPlayer* pSender);
    static void HandleCollected(const Packets::Pickups::PickupCollected& packet, CNetworkPlayer* pSender);
    static void OnPlayerConnected(CNetworkPlayer* pNetworkPlayer);
    static void OnPlayerDisconnected(CNetworkPlayer* pNetworkPlayer);

private:
    // while the lock holder stands on it (it asks again every 250 ms), a weapon pickup may wait for its Tab
    static constexpr server_time_t LOCK_TIME = 1000;

    struct SEntry
    {
        CNetworkPlayer* pLock = nullptr;
        server_time_t lockUntil = 0;
        bool bTaken = false;
        bool bForever = false;
        server_time_t backAt = 0;
    };

    static bool IsAway(const SEntry& entry);
    static int32_t GetRemaining(const SEntry& entry);

    static inline std::unordered_map<Packets::Pickups::PickupKey, SEntry, Packets::Pickups::PickupKeyHash> ms_entries;
};
