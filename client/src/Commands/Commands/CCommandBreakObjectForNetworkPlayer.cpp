#include "stdafx.h"
#include "CCommandBreakObjectForNetworkPlayer.h"
#include "network/packets/scripts.h"

void CCommandBreakObjectForNetworkPlayer::Process(CRunningScript* script)
{
	script->CollectParameters(4);

	auto pNetworkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[0]));
	if (!pNetworkPlayer)
	{
		return;
	}

	Packets::Scripts::NetworkPlayerObject packet{};
	packet.forWhoPlayerId = pNetworkPlayer->m_playerId;
	packet.broken = true;
	packet.pos = *(CVector*)&ScriptParams[1];
	GetPacketFactory().Send(packet);
}
