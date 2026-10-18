#include "stdafx.h"
#include "CCommandCreateObjectForNetworkPlayer.h"
#include "network/packets/scripts.h"

void CCommandCreateObjectForNetworkPlayer::Process(CRunningScript* script)
{
	script->CollectParameters(6);

	auto pNetworkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[0]));
	if (!pNetworkPlayer)
	{
		return;
	}

	Packets::Scripts::NetworkPlayerObject packet{};
	packet.forWhoPlayerId = pNetworkPlayer->m_playerId;
	// a model named in the script (#DYN_WINE_BIG) is minus its index in the script's object table, as CREATE_OBJECT
	// reads it
	int nModel = ScriptParams[1];
	if (nModel < 0)
	{
		nModel = CTheScripts::UsedObjectArray[-nModel].dwModelIndex;
	}
	packet.model = static_cast<uint16_t>(nModel);
	packet.pos = *(CVector*)&ScriptParams[2];
	packet.heading = *(float*)&ScriptParams[5];
	GetPacketFactory().Send(packet);
}
