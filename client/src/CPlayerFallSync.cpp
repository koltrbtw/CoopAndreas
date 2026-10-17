#include "stdafx.h"
#include "CPlayerFallSync.h"

// CTaskComplexFallAndGetUp (gta-reversed TaskComplexFallAndGetUp.h)
static constexpr unsigned int FALL_AND_GET_UP_ANIM_ID = 0xC;
static constexpr unsigned int FALL_AND_GET_UP_ANIM_GROUP = 0x10;
static constexpr unsigned int FALL_AND_GET_UP_DOWN_TIME = 0x14;

template <typename T>
static T TaskField(CTask* pTask, unsigned int nOffset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(pTask) + nOffset);
}

void CPlayerFallSync::Process()
{
    CPlayerPed* pPlayerPed = FindPlayerPed(0);
    if (!pPlayerPed)
    {
        return;
    }

    CTask* pFall = pPlayerPed->m_pIntelligence->m_TaskMgr.FindActiveTaskByType(TASK_COMPLEX_FALL_AND_GET_UP);
    if (pFall && pFall != ms_pLastFall && !pPlayerPed->m_nPedFlags.bInVehicle)
    {
        Packets::Players::PlayerFall packet{};
        packet.animGroup = static_cast<uint8_t>(TaskField<int>(pFall, FALL_AND_GET_UP_ANIM_GROUP));
        packet.animId = static_cast<uint16_t>(TaskField<int>(pFall, FALL_AND_GET_UP_ANIM_ID));
        packet.downTime = static_cast<uint16_t>(std::clamp(TaskField<int>(pFall, FALL_AND_GET_UP_DOWN_TIME), 0, 0xFFFF));
        GetPacketFactory().Send(packet);
    }
    ms_pLastFall = pFall;
}

void CPlayerFallSync::Reset()
{
    ms_pLastFall = nullptr;
}

void CPlayerFallSync::OnFallSent(CTask* pFall)
{
    ms_pLastFall = pFall;
}

void CPlayerFallSync::HandlePacket(const Packets::Players::PlayerFall& packet)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(packet.senderPlayerid);
    CPlayerPed* pPlayerPed = pNetworkPlayer ? pNetworkPlayer->m_pPed : nullptr;

    // in a vehicle here: its VEHICLE_EXIT is on the way and brings the fall
    if (!pPlayerPed || pPlayerPed->m_nPedFlags.bInVehicle)
    {
        return;
    }

    CVehicleExitSync::ApplyFall(pPlayerPed, Packets::Vehicles::VEHICLE_EXIT_FALL_ANIM, packet.animGroup, packet.animId,
        packet.downTime);
}
