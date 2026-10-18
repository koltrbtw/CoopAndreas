#include "stdafx.h"
#include "CPickupStates.h"

using namespace Packets::Pickups;

bool CPickupStates::IsAway(const SEntry& entry)
{
    return entry.bTaken && (entry.bForever || static_cast<int32_t>(entry.backAt - g_serverTime) > 0);
}

int32_t CPickupStates::GetRemaining(const SEntry& entry)
{
    return entry.bForever ? PickupCollected::FOREVER : static_cast<int32_t>(entry.backAt - g_serverTime);
}

void CPickupStates::HandleRequest(const PickupCollectRequest& packet, CNetworkPlayer* pSender)
{
    SEntry& entry = ms_entries[packet.key];

    // it missed the state (it came later than the collection, or its own game regenerated it sooner)
    if (IsAway(entry))
    {
        PickupState state{};
        state.key = packet.key;
        state.remainingMs = GetRemaining(entry);
        GetPacketFactory().Send(state, pSender);
        return;
    }

    if (entry.pLock && entry.pLock != pSender && static_cast<int32_t>(entry.lockUntil - g_serverTime) > 0)
    {
        return;
    }

    entry.pLock = pSender;
    entry.lockUntil = g_serverTime + LOCK_TIME;
    entry.bTaken = false;

    PickupGrant grant{};
    grant.key = packet.key;
    GetPacketFactory().Send(grant, pSender);
}

void CPickupStates::HandleCollected(const PickupCollected& packet, CNetworkPlayer* pSender)
{
    SEntry& entry = ms_entries[packet.key];
    entry.pLock = nullptr;
    entry.bTaken = true;
    entry.bForever = packet.regenerationMs == PickupCollected::FOREVER;
    entry.backAt = g_serverTime + static_cast<server_time_t>(std::max(packet.regenerationMs, 0));

    PickupState state{};
    state.key = packet.key;
    state.remainingMs = packet.regenerationMs;
    GetPacketFactory().SendToAll(state, pSender);
}

void CPickupStates::OnPlayerConnected(CNetworkPlayer* pNetworkPlayer)
{
    for (auto it = ms_entries.begin(); it != ms_entries.end();)
    {
        if (!IsAway(it->second))
        {
            it = ms_entries.erase(it);
            continue;
        }

        PickupState state{};
        state.key = it->first;
        state.remainingMs = GetRemaining(it->second);
        GetPacketFactory().Send(state, pNetworkPlayer);
        ++it;
    }
}

void CPickupStates::OnPlayerDisconnected(CNetworkPlayer* pNetworkPlayer)
{
    for (auto& [key, entry] : ms_entries)
    {
        if (entry.pLock == pNetworkPlayer)
        {
            entry.pLock = nullptr;
        }
    }
}
