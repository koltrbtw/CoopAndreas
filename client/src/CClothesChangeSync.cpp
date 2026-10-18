#include "stdafx.h"
#include "CClothesChangeSync.h"
#include <CVisibilityPlugins.h>

bool CClothesChangeSync::ms_bChanging = false;
bool CClothesChangeSync::ms_bRebuildHeld = false;

static bool IsScriptActive(const char* szName)
{
    for (auto* pScript = CTheScripts::pActiveScripts; pScript; pScript = pScript->m_pNext)
    {
        if (pScript->m_bIsActive && strnicmp(pScript->m_szName, szName, 8) == 0)
        {
            return true;
        }
    }
    return false;
}

bool CClothesChangeSync::IsLocalPlayerChanging()
{
    // gyms, Ammu-Nation, the arcade set the minigame flag too: the shop script must be the one running
    return CTheScripts::bMiniGameInProgress && (IsScriptActive("cloth") || IsScriptActive("warrobe"));
}

bool CClothesChangeSync::HoldRebuild()
{
    if (ms_bChanging)
    {
        ms_bRebuildHeld = true;
    }
    return ms_bChanging;
}

void CClothesChangeSync::Process()
{
    bool bChanging = IsLocalPlayerChanging();
    if (ms_bChanging && !bChanging && ms_bRebuildHeld)
    {
        // the outfit we left with (bought, or the old one restored), once
        if (CPlayerPed* pPlayer = FindPlayerPed(0))
        {
            Packets::Players::RebuildPlayer packet{};
            packet.clothesDesc = *pPlayer->m_pPlayerData->m_pPedClothesDesc;
            GetPacketFactory().Send(packet);
        }
        ms_bRebuildHeld = false;
    }
    ms_bChanging = bChanging;

    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        CPlayerPed* pPed = pNetworkPlayer->m_pPed;
        if (!pPed)
        {
            continue;
        }

        bool bHidden = bChanging || (pNetworkPlayer->m_nLastStateFlags & CPedStateSync::STATE_CHANGING_CLOTHES) != 0;
        // the shop's mirror draws the room once more on its own pass, m_bIsVisible doesn't keep the clone out of it:
        // no alpha, every frame (the ped's fade code sets its own)
        if (bHidden && pPed->m_pRwClump)
        {
            CVisibilityPlugins::SetClumpAlpha(pPed->m_pRwClump, 0);
        }
        if (bHidden == pNetworkPlayer->m_bHiddenWhileChanging)
        {
            continue;
        }

        // the shop camera looks at the changing spot, where the other one may be changing as well: no body, no
        // collision (the script's teleport onto the spot and ClearArea would push it)
        pNetworkPlayer->m_bHiddenWhileChanging = bHidden;
        pPed->m_bIsVisible = !bHidden;
        pPed->m_bUsesCollision = !bHidden;
        if (!bHidden && pPed->m_pRwClump)
        {
            CVisibilityPlugins::SetClumpAlpha(pPed->m_pRwClump, 255);
        }
        logger::info("%s %s while changing clothes", pNetworkPlayer->GetName().c_str(), bHidden ? "hidden" : "shown");
    }
}
