#include "stdafx.h"
#include "CWantedSync.h"

void CWantedSync::Reset()
{
    // send our level to the server even if it's 0
    ms_nLastSentWantedLevel = -1;
}

unsigned int CWantedSync::GetGroupMaxWantedLevel(const CVector& localPosition)
{
    // players linked by a chain of neighbours closer than GROUP_RADIUS are one group
    std::vector<CVector> groupPositions = {localPosition};
    std::vector<CNetworkPlayer*> others;

    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer->m_pPed)
        {
            others.push_back(pNetworkPlayer);
        }
    }

    unsigned int nMaxWantedLevel = 0;
    bool bAdded = true;
    while (bAdded)
    {
        bAdded = false;
        for (auto it = others.begin(); it != others.end();)
        {
            CVector position = (*it)->m_pPed->GetPosition();
            bool bInGroup = std::any_of(groupPositions.begin(), groupPositions.end(),
                [&](const CVector& groupPosition) { return (groupPosition - position).Magnitude() < GROUP_RADIUS; });

            if (!bInGroup)
            {
                ++it;
                continue;
            }

            nMaxWantedLevel = std::max<unsigned int>(nMaxWantedLevel, (*it)->m_nWantedLevel);
            groupPositions.push_back(position);
            it = others.erase(it);
            bAdded = true;
        }
    }

    return nMaxWantedLevel;
}

void CWantedSync::Process()
{
    CPlayerPed* pLocalPlayer = FindPlayerPed(0);
    CWanted* pWanted = FindPlayerWanted(0);
    if (pLocalPlayer == nullptr || pWanted == nullptr)
    {
        return;
    }

    // only a player who is wanted himself joins the gang's level, standing next to it is fine
    if (pWanted->m_nWantedLevel > 0)
    {
        unsigned int nGroupWantedLevel = GetGroupMaxWantedLevel(pLocalPlayer->GetPosition());
        if (nGroupWantedLevel > pWanted->m_nWantedLevel)
        {
            pWanted->SetWantedLevelNoDrop(nGroupWantedLevel);
        }
    }

    int nWantedLevel = static_cast<int>(pWanted->m_nWantedLevel);
    if (nWantedLevel != ms_nLastSentWantedLevel)
    {
        Packets::Players::PlayerWantedLevel packet{};
        packet.wantedLevel = static_cast<uint8_t>(std::clamp(nWantedLevel, 0, 6));
        GetPacketFactory().Send(packet);

        ms_nLastSentWantedLevel = nWantedLevel;
    }
}

void CWantedSync::HandlePacket(Packets::Players::PlayerWantedLevel* pPlayerWantedLevel)
{
    if (auto pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPlayerWantedLevel->senderPlayerid))
    {
        pNetworkPlayer->m_nWantedLevel = pPlayerWantedLevel->wantedLevel;
    }
}
