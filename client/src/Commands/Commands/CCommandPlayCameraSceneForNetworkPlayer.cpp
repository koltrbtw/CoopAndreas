#include "stdafx.h"
#include "CCommandPlayCameraSceneForNetworkPlayer.h"
#include "network/packets/scripts.h"

void CCommandPlayCameraSceneForNetworkPlayer::Process(CRunningScript* script)
{
	Packets::Scripts::NetworkPlayerCameraScene packet{};
	script->ReadTextLabelFromScript(packet.text, 8);
	packet.text[7] = '\0';
	script->ReadTextLabelFromScript(packet.help, 8);
	packet.help[7] = '\0';

	script->CollectParameters(9);

	auto pNetworkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[0]));
	if (!pNetworkPlayer)
	{
		return;
	}

	packet.forWhoPlayerId = pNetworkPlayer->m_playerId;
	packet.camPos = *(CVector*)&ScriptParams[1];
	packet.lookAt = *(CVector*)&ScriptParams[4];
	packet.delay = static_cast<uint16_t>(ScriptParams[7]);
	packet.duration = static_cast<uint16_t>(ScriptParams[8]);
	GetPacketFactory().Send(packet);
}
