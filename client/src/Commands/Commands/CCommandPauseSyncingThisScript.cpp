#include "stdafx.h"
#include "CCommandPauseSyncingThisScript.h"
#include "COpCodeSync.h"

void CCommandPauseSyncingThisScript::Process(CRunningScript* pScript)
{
	pScript->CollectParameters(1);

	for (size_t i = 0; i < COpCodeSync::ms_iFreeSyncedScript; i++)
	{
		if (strnicmp(COpCodeSync::ms_aszSyncedScripts[i], pScript->m_szName, 7) == 0)
		{
			COpCodeSync::ms_abSyncedScriptPaused[i] = ScriptParams[0] != 0;
			return;
		}
	}
}
