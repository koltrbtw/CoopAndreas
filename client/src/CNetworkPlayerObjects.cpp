#include "stdafx.h"
#include "CNetworkPlayerObjects.h"
#include "network/packets/scripts.h"

std::vector<int> CNetworkPlayerObjects::ms_handles;

void CNetworkPlayerObjects::Add(int nModel, const CVector& pos, float fHeading)
{
    if (nModel <= 0 || nModel >= 20000 || !CModelInfo::GetModelInfo(nModel))
    {
        logger::info("network player object: bad model %d", nModel);
        return;
    }

    // the mission requested the model on the host only
    CStreaming::RequestModel(nModel, GAME_REQUIRED | KEEP_IN_MEMORY);
    CStreaming::LoadAllRequestedModels(false);
    if (!CStreaming::ms_aInfoForModel[nModel].m_nLoadState)
    {
        logger::info("network player object: model %d not loaded", nModel);
        return;
    }

    // a mission object as the script makes it (OBJECT_MISSION): CWorldObjectSync leaves it alone and the shots of the
    // others don't hit it here (PLAYER_BULLET_SHOT)
    int nHandle = 0;
    plugin::Command<Commands::CREATE_OBJECT>(nModel, pos.x, pos.y, pos.z, &nHandle);
    plugin::Command<Commands::SET_OBJECT_HEADING>(nHandle, fHeading);
    plugin::Command<Commands::MAKE_OBJECT_TARGETTABLE>(nHandle, true);
    CStreaming::SetMissionDoesntRequireModel(nModel);
    ms_handles.push_back(nHandle);
    logger::info("network player object: model %d at %.1f %.1f %.1f, handle %d", nModel, pos.x, pos.y, pos.z, nHandle);
}

void CNetworkPlayerObjects::RemoveAll()
{
    for (int nHandle : ms_handles)
    {
        if (CPools::GetObject(nHandle))
        {
            plugin::Command<Commands::DELETE_OBJECT>(nHandle);
        }
    }
    ms_handles.clear();
}

void CNetworkPlayerObjects::Break(const CVector& pos)
{
    constexpr float MAX_DISTANCE = 0.5f;
    for (int nHandle : ms_handles)
    {
        CObject* pObject = CPools::GetObject(nHandle);
        if (pObject && (pObject->GetPosition() - pos).Magnitude() < MAX_DISTANCE)
        {
            plugin::Command<Commands::BREAK_OBJECT>(nHandle, 1);
            return;
        }
    }
}

void CNetworkPlayerObjects::Process()
{
    for (auto it = ms_handles.begin(); it != ms_handles.end();)
    {
        int nHandle = *it;
        if (!CPools::GetObject(nHandle))
        {
            it = ms_handles.erase(it);
            continue;
        }

        // as the mission does with its own: a hit bottle shatters, the object goes
        if (plugin::Command<Commands::HAS_OBJECT_BEEN_DAMAGED>(nHandle))
        {
            plugin::Command<Commands::MAKE_OBJECT_TARGETTABLE>(nHandle, false);
            plugin::Command<Commands::DELETE_OBJECT>(nHandle);
            it = ms_handles.erase(it);

            Packets::Scripts::NetworkPlayerObjectBroken packet{};
            GetPacketFactory().Send(packet);
            continue;
        }
        ++it;
    }
}
