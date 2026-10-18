#include "stdafx.h"
#include <CTaskSimpleCarSetPedOut.h>
#include <CCarEnterExit.h>
#include <CTaskSimpleCarSetPedInAsPassenger.h>
#include <CTaskComplexEnterCarAsPassenger.h>
#include <CTaskComplexEnterCar.h>

CNetworkPlayer::~CNetworkPlayer()
{
    if (m_pPed)
    {
        m_pPed->CleanUpOldReference((CEntity**)&m_pPed);
        DestroyPed();
    }
}

CNetworkPlayer::CNetworkPlayer(playerid_t id, CVector position)
{
    m_playerId = id;
    m_nSlot = CNetworkPlayerManager::GetFreeSlot();
    assert(m_nSlot != -1);

    m_pPedClothesDesc.SetTextureAndModel("VEST", "VEST", 0);
    m_pPedClothesDesc.SetTextureAndModel("JEANSDENIM", "JEANS", 2);
    m_pPedClothesDesc.SetTextureAndModel("SNEAKERBINCBLK", "SNEAKER", 3);
    m_pPedClothesDesc.SetTextureAndModel("PLAYER_FACE", "HEAD", 1);

    CreatePed(position);
}

void CNetworkPlayer::CreatePed(CVector position)
{
    unsigned int actorId = 0;
    int playerId = m_nSlot + 2;

    // TODO: get rid of the ugly scm command and call in-game funcs
    plugin::Command<Commands::CREATE_PLAYER>(playerId, position.x, position.y, position.z, &actorId);
    plugin::Command<Commands::GET_PLAYER_CHAR>(playerId, &actorId);

    m_pPed = (CPlayerPed*)CPools::GetPed(actorId);
    m_pPed->RegisterReference((CEntity**)&m_pPed);

    m_pPed->SetOrientation(0.0f, 0.0f, 0.0f);

    // set player immunies, he doesn't care about the pain now
    Command<Commands::SET_CHAR_PROOFS>(actorId, 0, 1, 1, 0, 0);

    *m_pPed->m_pPlayerData->m_pPedClothesDesc = m_pPedClothesDesc;

    CClothes::RebuildPlayer(m_pPed, false);
    m_bHasTargetPos = false;  // the first update puts it there

    // a jacker of this game would drag it out here only, the sender keeps driving; a player jacking it is let
    // through by CPlayerPed__ProcessControl_Hook, the sender plays our enter task too
    m_pPed->m_nPedFlags.bDontDragMeOutCar = 1;

    // THIS IS AN EXPERIMENTAL SOLUTION FOR THE 0x4D68BA CRASH
    m_pPed->m_bStreamingDontDelete = true;
}

void CNetworkPlayer::DestroyPed()
{
    if (m_pPed->m_pVehicle)
    {
        plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(CPools::GetPedRef(m_pPed), 0.f, 0.f, 0.f);
    }

    m_pPed->CleanUpOldReference((CEntity**)&m_pPed);

    if (m_pPed->IsVTableValid())
    {
        CWorld::Remove(m_pPed);
        delete m_pPed;
    }
    m_pPed = nullptr;
}

void CNetworkPlayer::Respawn()
{
    if (m_pPed)
    {
        this->DestroyPed();
    }

    this->CreatePed(m_onFootSnapshotInterpolated.vecPos);
}

int CNetworkPlayer::GetInternalId()  // most used for CWorld::PlayerInFocus
{
    byte playerNumber = 0;

    for (; playerNumber < Config::MAX_SERVER_PLAYERS + 2; playerNumber++)
    {
        if (m_pPed == CWorld::Players[playerNumber].m_pPed)
        {
            return playerNumber;
        }
    }

    return -1;
}

std::string CNetworkPlayer::GetName()
{
    if (m_Name[0] == '\0')
    {
        char buffer[32 + 1];
        sprintf(buffer, "player %u", m_playerId.value());
        return buffer;
    }

    return m_Name;
}

char CNetworkPlayer::GetWeaponSkill(eWeaponType weaponType)
{
    if (weaponType < WEAPON_PISTOL || weaponType > WEAPON_TEC9)
        return 1;

    eStats weaponStatId = plugin::CallAndReturn<eStats, 0x743CD0>(weaponType);  // CWeaponInfo::GetSkillStatIndex
    int statSyncId = CStatsSync::GetSyncIdByInternal(weaponStatId);
    float weaponStat = m_stats[weaponStatId];

    if (CWeaponInfo::GetWeaponInfo(weaponType, 2)->m_nReqStatLevel <= weaponStat)
        return 2;

    return CWeaponInfo::GetWeaponInfo(weaponType, 1)->m_nReqStatLevel <= weaponStat;
}

float CNetworkPlayer::GetMaxHealth()
{
    // same as CStats::GetFatAndMuscleModifier(STAT_MOD_MAX_HEALTH) (0x559E07), but with this player's stat
    return m_stats[STAT_MAX_HEALTH] * 0.001f * 176.0f;
}

void CNetworkPlayer::WarpIntoVehicleDriver(CVehicle* vehicle)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    // the warp would throw the player out, it says itself when it got out (VEHICLE_EXIT)
    if (CNetworkVehicle::IsSeatHeldByPlayer(vehicle, -1, m_pPed))
    {
        return;
    }

    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle)
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    m_pPed->m_pIntelligence->FlushImmediately(false);

    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1;  // 1 - never

    auto task = CTaskSimpleCarSetPedInAsDriver(vehicle, nullptr);
    task.m_bWarpingInToCar = true;
    task.ProcessPed(m_pPed);
}

void CNetworkPlayer::WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    if (CNetworkVehicle::IsSeatHeldByPlayer(vehicle, seatid, m_pPed))
    {
        return;
    }

    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle)
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    m_pPed->m_pIntelligence->FlushImmediately(false);

    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1;  // 1 - never

    int doorId = CCarEnterExit::ComputeTargetDoorToEnterAsPassenger(vehicle, seatid);
    auto task = CTaskSimpleCarSetPedInAsPassenger(vehicle, doorId, nullptr);
    task.m_bWarpingInToCar = true;
    task.ProcessPed(m_pPed);
}

void CNetworkPlayer::EnterVehiclePassenger(CVehicle* vehicle, int seatid)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle)
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    m_pPed->m_pIntelligence->FlushImmediately(false);

    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1;  // 1 - never

    int doorId = CCarEnterExit::ComputeTargetDoorToEnterAsPassenger(vehicle, seatid);
    auto task = new CTaskComplexEnterCarAsPassenger(vehicle, doorId, false);
    m_pPed->m_pIntelligence->m_TaskMgr.SetTask(task, 3, false);
}

// an enter (from the door walk to the door closed) takes a few seconds; past that the updates of the sender win
static constexpr unsigned int ENTER_VEHICLE_TIMEOUT = 6000;

void CNetworkPlayer::OnEnterVehicle(vehicleid_t vehicleid)
{
    m_enterVehicleId = vehicleid;
    m_nEnterVehicleTime = CTimer::m_snTimeInMilliseconds;
    m_nExitVehicleTime = 0;
    m_nJackAllowedUntil = 0;  // its own enter now, whatever jack was on the way
    m_pendingExit.reset();
}

void CNetworkPlayer::AllowJack(unsigned int nTime)
{
    m_nJackAllowedUntil = CTimer::m_snTimeInMilliseconds + nTime;
    if (m_pPed)
    {
        m_pPed->m_nPedFlags.bDontDragMeOutCar = 0;
    }
}

bool CNetworkPlayer::IsJackAllowed()
{
    return CTimer::m_snTimeInMilliseconds < m_nJackAllowedUntil;
}

bool CNetworkPlayer::HasJustLeftVehicle(vehicleid_t vehicleid)
{
    // longer than a vehicle update can lag behind an event
    constexpr unsigned int LATE_UPDATE_TIME = 1000;
    return m_nExitVehicleTime != 0 && m_exitVehicleId == vehicleid &&
           CTimer::m_snTimeInMilliseconds - m_nExitVehicleTime < LATE_UPDATE_TIME;
}

bool CNetworkPlayer::IsEnteringVehicle(CVehicle* vehicle)
{
    if (m_pPed == nullptr || m_nEnterVehicleTime == 0 ||
        CTimer::m_snTimeInMilliseconds - m_nEnterVehicleTime > ENTER_VEHICLE_TIMEOUT)
    {
        return false;
    }

    auto& taskManager = m_pPed->m_pIntelligence->m_TaskMgr;
    for (int taskType : {TASK_COMPLEX_ENTER_CAR_AS_PASSENGER, TASK_COMPLEX_ENTER_CAR_AS_DRIVER})
    {
        auto pTask = static_cast<CTaskComplexEnterCar*>(taskManager.FindActiveTaskByType(taskType));
        if (pTask && pTask->m_pTargetVehicle == vehicle)
        {
            return true;
        }
    }

    return false;
}

bool CNetworkPlayer::HasJustEnteredVehicle()
{
    if (m_pPed == nullptr || m_nEnterVehicleTime == 0 ||
        CTimer::m_snTimeInMilliseconds - m_nEnterVehicleTime > ENTER_VEHICLE_TIMEOUT)
    {
        return false;
    }

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(m_enterVehicleId);
    return pNetworkVehicle && m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle == pNetworkVehicle->m_pVehicle;
}

void CNetworkPlayer::RemoveFromVehicle(CVehicle* vehicle, bool bKnockedOff)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    m_pPed->m_pIntelligence->m_TaskMgr.SetTask(nullptr, TASK_PRIMARY_PRIMARY, false);

    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 2;  // 2 - normal

    auto task = CTaskSimpleCarSetPedOut(vehicle, 1, false);
    task.m_bWarpingOutOfCar = true;
    task.m_bKnockedOffBike = bKnockedOff;
    task.ProcessPed(m_pPed);
}

// a leave takes a few seconds (slowing down, the get out anim, the door); past that the updates of the sender win
static constexpr unsigned int LEAVE_VEHICLE_TIMEOUT = 6000;

void CNetworkPlayer::LeaveVehicle(CVehicle* vehicle)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 2;  // 2 - normal
    m_nLeaveVehicleTime = CTimer::m_snTimeInMilliseconds;

    auto* task = new CTaskComplexLeaveCar(vehicle, 0, 0, true, false);
    m_pPed->m_pIntelligence->m_TaskMgr.SetTask(task, TASK_PRIMARY_PRIMARY, false);
}

bool CNetworkPlayer::IsLeavingVehicle(CVehicle* vehicle)
{
    if (m_pPed == nullptr)
    {
        return false;
    }

    // a player jacking it here plays the same jack on the sender's game (VEHICLE_ENTER), the drag out ends by itself
    auto& taskManager = m_pPed->m_pIntelligence->m_TaskMgr;
    for (int taskType : {TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT_AND_STAND_UP, TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT,
             TASK_COMPLEX_CAR_QUICK_BE_DRAGGED_OUT, TASK_SIMPLE_BIKE_JACKED})
    {
        if (taskManager.FindActiveTaskByType(taskType))
        {
            return true;
        }
    }

    // our copy of the jacker is still on its way to the door, it lags behind the sender's jacker: the sender is out
    // already, its clone waits for our drag out
    if (IsJackAllowed() && m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle == vehicle)
    {
        return true;
    }

    if (m_nLeaveVehicleTime == 0 || CTimer::m_snTimeInMilliseconds - m_nLeaveVehicleTime > LEAVE_VEHICLE_TIMEOUT)
    {
        return false;
    }

    auto pTask = static_cast<CTaskComplexLeaveCar*>(taskManager.FindActiveTaskByType(TASK_COMPLEX_LEAVE_CAR));
    return pTask && pTask->m_pTargetVehicle == vehicle;
}

void CNetworkPlayer::HandleTask(Packets::Players::SetPlayerTask& packet)
{
    if (!m_pPed)
    {
        return;
    }

#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("HandleTask %d toggle %d", packet.taskType, packet.toggle);
#endif

    m_pPed->SetPosn(packet.vecPos);
    m_pPed->m_fCurrentRotation = packet.currentRotation.m_angle;
    m_pPed->m_fAimingRotation = packet.aimingRotation.m_angle;

    CTask* activeTask = m_pPed->m_pIntelligence->m_TaskMgr.GetActiveTask();
    eTaskType activeTaskType = activeTask ? activeTask->GetTaskType() : TASK_NONE;
    switch ((eTaskType)packet.taskType)
    {
        case eTaskType::TASK_COMPLEX_JUMP:
        {
            m_pPed->ClearWeaponTarget();
            if (activeTask && activeTaskType == packet.taskType)  // if the jump task is active
            {
                return;
            }

            if (m_pPed->m_pIntelligence->GetTaskDuck(false))
            {
                CTaskSimpleDuckToggle(0).ProcessPed(m_pPed);
            }

            m_pPed->m_pIntelligence->m_TaskMgr.SetTask(new CTaskComplexJump(0), 3, false);

            break;
        }
        case eTaskType::TASK_SIMPLE_DUCK:
        {
            m_bRequestedDuckTask = true;
            // if (packet.toggle)
            //{
            //     m_pPed->m_pIntelligence->SetTaskDuckSecondary(0);

            //    CTaskSimpleFight* pFightingTask = m_pPed->m_pIntelligence->GetTaskFighting();
            //    if (pFightingTask)
            //    {
            //        // abort fighting task (0x6879C2)
            //        plugin::CallMethodAndReturn<bool, 0x61C5E0, CTaskSimpleFight*, CEntity*, int8_t>(pFightingTask,
            //        nullptr, 18);
            //    }
            //}
            // else
            //{
            //    m_pPed->m_pIntelligence->ClearTaskDuckSecondary();
            //}
            break;
        }
    }
}

void CNetworkPlayer::ApplyWeaponSnapshot(Packets::Players::SWeaponSnapshot& weaponSnapshot)
{
    if (m_pPed == nullptr)
    {
        return;
    }

    m_onFootSnapshotInterpolated.weaponSnapshot = weaponSnapshot;
    // TODO refactor CUtil
    CUtil::GiveWeaponByPacket(this, weaponSnapshot.iWeaponType, weaponSnapshot.nAmmo);
    m_pPed->m_aWeapons[m_pPed->m_nActiveWeaponSlot].m_nState = static_cast<eWeaponState>(weaponSnapshot.iWeaponState);
}
