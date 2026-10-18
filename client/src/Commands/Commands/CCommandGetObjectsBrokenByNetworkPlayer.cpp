#include "stdafx.h"
#include "CCommandGetObjectsBrokenByNetworkPlayer.h"

void CCommandGetObjectsBrokenByNetworkPlayer::Process(CRunningScript* script)
{
	script->CollectParameters(1);

	auto pNetworkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[0]));
	ScriptParams[0] = pNetworkPlayer ? pNetworkPlayer->m_nObjectsBroken : 0;

	script->StoreParameters(1);
}
