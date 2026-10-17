#pragma once

#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <algorithm>

#include "CNetworkPlayer.h"

class CNetworkPlayerManager
{
public:
    static std::vector<CNetworkPlayer*> m_pPlayers;
    static void Add(CNetworkPlayer* player);
    static void Remove(CNetworkPlayer* player);
    static CNetworkPlayer* GetPlayer(playerid_t playerid);
    static CNetworkPlayer* GetPlayer(ENetPeer* peer);
    static playerid_t AllocatePlayerId();
    static CNetworkPlayer* GetHost();
    // ambient entities of a leaving player go to the nearest player within this distance
    static constexpr float HANDOVER_ON_LEAVE_DISTANCE = 300.0f;
    static CNetworkPlayer* GetNearestPlayer(const CVector& position, float maxDistance, CNetworkPlayer* except);
    static void AssignHostToFirstPlayer();

private:
    static uint32_t ms_nNextPlayerId;
};