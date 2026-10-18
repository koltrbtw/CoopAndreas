#include "stdafx.h"
#include "CCommandRemoveObjectsForNetworkPlayer.h"
#include "network/packets/scripts.h"

void CCommandRemoveObjectsForNetworkPlayer::Process(CRunningScript* script)
{
	script->CollectParameters(1);

	auto pNetworkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[0]));
	if (!pNetworkPlayer)
	{
		return;
	}

	// what he breaks from now on counts for the next set
	pNetworkPlayer->m_nObjectsBroken = 0;

	Packets::Scripts::NetworkPlayerObject packet{};
	packet.forWhoPlayerId = pNetworkPlayer->m_playerId;
	packet.clear = true;
	GetPacketFactory().Send(packet);
}
