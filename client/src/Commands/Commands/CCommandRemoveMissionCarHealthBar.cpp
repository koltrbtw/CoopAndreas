#include "stdafx.h"
#include "CCommandRemoveMissionCarHealthBar.h"
#include "CMissionCarHealthBar.h"
#include "network/packets/scripts.h"

// Coop.RemoveMissionCarHealthBar(player_actor)
void CCommandRemoveMissionCarHealthBar::Process(CRunningScript* script)
{
    script->CollectParameters(1);
    CPed* pPed = CPools::GetPed(ScriptParams[0]);
    if (pPed && pPed == FindPlayerPed(0))
    {
        CMissionCarHealthBar::Clear();
    }
    else if (auto pNetworkPlayer = pPed ? CNetworkPlayerManager::GetPlayer(pPed) : nullptr)
    {
        Packets::Scripts::NetworkPlayerCarHealthBar packet{};
        packet.forWhoPlayerId = pNetworkPlayer->m_playerId;
        packet.show = false;
        GetPacketFactory().Send(packet);
    }
}
