#include "stdafx.h"
#include <CPedGroups.h>
#include "CNetworkPed.h"
#include <CTaskSimpleCarSetPedInAsPassenger.h>
#include <CCarEnterExit.h>
#include <CTaskSimpleCarSetPedOut.h>
#include <Hooks/PedHooks.h>
#include "CPedAnimSync.h"
#include <CTaskComplexEnterCar.h>
#include <CTaskComplexEnterCarAsDriver.h>
#include <CTaskComplexEnterCarAsPassenger.h>

CNetworkPed::CNetworkPed(pedid_t pedid, int modelId, ePedType pedType, CVector pos, unsigned char createdBy, char specialModelName[])
{
    if (modelId >= 290 && modelId <= 299)
        CStreaming::RequestSpecialModel(modelId, specialModelName, 0);
    else
        CStreaming::RequestModel(modelId, 0);

    CStreaming::LoadAllRequestedModels(false);

    if (pedType == PED_TYPE_COP)
    {
        switch (modelId) 
        {
        case MODEL_LAPDM1:
            modelId = COP_TYPE_LAPDM1;
            break;
        case MODEL_CSHER:
            modelId = COP_TYPE_CSHER;
            break;
        case MODEL_SWAT:
            modelId = COP_TYPE_SWAT1;
            break;
        case MODEL_FBI:
            modelId = COP_TYPE_FBI;
            break;
        case MODEL_ARMY:
            modelId = COP_TYPE_ARMY;
            break;
        }
    }

    if (pedType == PED_TYPE_COP)
    {
        m_pPed = new CCopPed((eCopType)modelId);
    }
    else if (pedType == PED_TYPE_MEDIC || pedType == PED_TYPE_FIREMAN)
    {
        m_pPed = new CEmergencyPed(pedType, modelId);
    }
    else
    {
        m_pPed = new CCivilianPed(pedType, modelId);
    }

    assert(m_pPed && "could not allocate a slot for a new network ped, ped pool limit reached");
    
    m_pPed->RegisterReference((CEntity**)&m_pPed);

    m_pPed->m_nCreatedBy = 2;
    m_pPed->m_pIntelligence->SetPedDecisionMakerType(-1);
    m_pPed->m_pIntelligence->SetSeeingRange(30.0);
    m_pPed->m_pIntelligence->SetHearingRange(30.0);
    m_pPed->m_pIntelligence->m_fDmRadius = 0.0f;
    m_pPed->m_pIntelligence->m_nDmNumPedsToScan = 0;
    
    m_pPed->SetPosn(pos);
    m_pPed->SetOrientation(0.f, 0.f, 0.f);
    CWorld::Add(m_pPed);

    m_pedId = pedid;
    m_nPedType = pedType;
    m_bSyncing = false;
    m_nCreatedBy = createdBy;

    // THIS IS AN EXPERIMENTAL SOLUTION FOR THE 0x4D68BA CRASH
    m_pPed->m_bStreamingDontDelete = true;
}

CNetworkPed::~CNetworkPed()
{
    if (m_pPed)
    {
        m_pPed->CleanUpOldReference((CEntity**)&m_pPed);
    }

    if (m_bSyncing)
    {
        Packets::Peds::PedRemove packet{};
        packet.pedid = m_pedId;
        GetPacketFactory().Send(packet);
    }
    else
    {

        if (m_pPed && m_pPed->m_matrix->m_pOwner)
        {
            if (m_nBlipHandle != -1)
            {
                CRadar::ClearBlipForEntity(eBlipType::BLIP_CHAR, CPools::GetPedRef(m_pPed));
                //CChat::AddMessage("REMOVE THE FUCKING BLIP");
            }

            if (m_pPed->m_nPedFlags.bInVehicle)
            {
                plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(CPools::GetPedRef(m_pPed), 0.f, 0.f, 0.f);
            }

            CWorld::Remove(m_pPed);
            //CWorld::RemoveReferencesToDeletedObject(m_pPed);
            m_pPed->Remove();
            delete m_pPed;
        }
    }
    m_pPed = nullptr;
}

CNetworkPed* CNetworkPed::CreateHosted(CPed* ped)
{
    // m_nTimeTillWeNeedThisPed is an absolute time, keep the new ped for at least 5 seconds
    ped->field_54C = std::max<int>(ped->field_54C, static_cast<int>(CTimer::m_snTimeInMilliseconds + 5000));

    CNetworkPed* networkPed = new CNetworkPed();

    networkPed->m_pPed = ped;
    ped->RegisterReference((CEntity**)&networkPed->m_pPed);

    networkPed->m_pedId = CNetworkPedManager::AllocatePedId();
    networkPed->m_nCreatedBy = ped->m_nCreatedBy;
    networkPed->m_bSyncing = true;

    CNetworkPedManager::Add(networkPed);

    Packets::Peds::PedSpawn packet{};
    packet.pedid = networkPed->m_pedId;
    packet.modelId = static_cast<eModelID>(ped->m_nModelIndex);
    packet.pos = ped->m_matrix->pos;
    packet.pedType = static_cast<ePedType>(ped->m_nPedType);
    packet.createdBy = static_cast<eCharCreatedBy>(ped->m_nCreatedBy);

    if (packet.modelId >= MODEL_SPECIAL01 && packet.modelId <= MODEL_SPECIAL10)
    {
        strcpy_s(packet.specialModelName, PedHooks::ms_aszLoadedSpecialModels[packet.modelId - MODEL_SPECIAL01]);
        packet.specialModelName[7] = '\0';
    }
    GetPacketFactory().Send(packet);

    return networkPed;
}

void CNetworkPed::WarpIntoVehicleDriver(CVehicle* vehicle)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    // the warp would throw a player out (ours too): its own game takes it out, then the next update gets us in
    if (CNetworkVehicle::IsSeatHeldByPlayer(vehicle, -1, m_pPed))
    {
        return;
    }

    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle)
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    m_pPed->m_pIntelligence->FlushImmediately(false);

    if (!m_bSyncing)
    {
        m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1; // 1 - never
    }

    auto task = CTaskSimpleCarSetPedInAsDriver(vehicle, nullptr);
    task.m_bWarpingInToCar = true;
    task.ProcessPed(m_pPed);
}

void CNetworkPed::WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid)
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

    if (!m_bSyncing)
    {
        m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1; // 1 - never
    }

    int doorId = CCarEnterExit::ComputeTargetDoorToEnterAsPassenger(vehicle, seatid);
    auto task = CTaskSimpleCarSetPedInAsPassenger(vehicle, doorId, nullptr);
    task.m_bWarpingInToCar = true;
    task.ProcessPed(m_pPed);
}

void CNetworkPed::RemoveFromVehicle(CVehicle* vehicle, bool bKnockedOff)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    m_pPed->m_pIntelligence->m_TaskMgr.SetTask(nullptr, TASK_PRIMARY_PRIMARY, false);

    if (!m_bSyncing)
    {
        m_pPed->m_nPedFlags.CantBeKnockedOffBike = 2; // 2 - normal
    }

    auto task = CTaskSimpleCarSetPedOut(vehicle, 1, false);
    task.m_bWarpingOutOfCar = true;
    task.m_bKnockedOffBike = bKnockedOff;
    task.ProcessPed(m_pPed);

    m_pPed->m_pIntelligence->FlushImmediately(true); // create a default primary task (fix bug)
}

// a leave takes a few seconds (slowing down, the get out anim, the door); past that the updates of the syncer win
static constexpr unsigned int LEAVE_VEHICLE_TIMEOUT = 6000;

void CNetworkPed::LeaveVehicle(CVehicle* vehicle)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    m_nLeaveVehicleTime = CTimer::m_snTimeInMilliseconds;
    m_pPed->m_pIntelligence->m_TaskMgr.SetTask(new CTaskComplexLeaveCar(vehicle, 0, 0, true, false),
        TASK_PRIMARY_PRIMARY, false);
}

bool CNetworkPed::IsLeavingVehicle(CVehicle* vehicle)
{
    if (m_pPed == nullptr)
    {
        return false;
    }

    // a player jacking it here plays the same jack on the syncer's game (VEHICLE_ENTER), the drag out ends by itself
    auto& taskManager = m_pPed->m_pIntelligence->m_TaskMgr;
    for (int taskType : {TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT_AND_STAND_UP, TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT,
             TASK_COMPLEX_CAR_QUICK_BE_DRAGGED_OUT, TASK_SIMPLE_BIKE_JACKED})
    {
        if (taskManager.FindActiveTaskByType(taskType))
        {
            return true;
        }
    }

    if (m_nLeaveVehicleTime == 0 || CTimer::m_snTimeInMilliseconds - m_nLeaveVehicleTime > LEAVE_VEHICLE_TIMEOUT)
    {
        return false;
    }

    auto pTask = static_cast<CTaskComplexLeaveCar*>(taskManager.FindActiveTaskByType(TASK_COMPLEX_LEAVE_CAR));
    return pTask && pTask->m_pTargetVehicle == vehicle;
}

bool CNetworkPed::HasJustLeftVehicle(vehicleid_t vehicleid)
{
    // longer than a vehicle update can lag behind an event
    constexpr unsigned int LATE_UPDATE_TIME = 1000;
    return m_nExitVehicleTime != 0 && m_exitVehicleId == vehicleid &&
           CTimer::m_snTimeInMilliseconds - m_nExitVehicleTime < LATE_UPDATE_TIME;
}

// the walk to the door and the get in (or a jacking) take seconds; past that the updates of the syncer win
static constexpr unsigned int ENTER_VEHICLE_TIMEOUT = 10000;

void CNetworkPed::EnterVehicle(CVehicle* vehicle, bool bPassenger, int seat, playerid_t syncer)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid() || m_pPed->m_nPedFlags.bInVehicle)
    {
        return;
    }

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(vehicle);
    if (!pNetworkVehicle)
    {
        return;
    }

    m_enterVehicleId = pNetworkVehicle->m_vehicleId;
    m_nEnterVehicleTime = CTimer::m_snTimeInMilliseconds;
    m_bHasTargetPos = false;
    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1;  // 1 - never

    // a remote player sitting there is jacked by its own game too when the syncer of the jacker is that player:
    // let our copy drag its clone out (CNetworkPlayer::AllowJack)
    CPed* apOccupants[1 + 8] = {vehicle->m_pDriver};
    std::copy(std::begin(vehicle->m_apPassengers), std::end(vehicle->m_apPassengers), apOccupants + 1);
    for (CPed* pOccupant : apOccupants)
    {
        CNetworkPlayer* pNetworkPlayer = pOccupant ? CNetworkPlayerManager::GetPlayer(pOccupant) : nullptr;
        if (pNetworkPlayer && pNetworkPlayer->m_playerId == syncer)
        {
            pNetworkPlayer->AllowJack(ENTER_VEHICLE_TIMEOUT);
        }
    }
    CNetworkPlayer* pDriverPlayer = vehicle->m_pDriver ? CNetworkPlayerManager::GetPlayer(vehicle->m_pDriver) : nullptr;
    logger::info("ped %s of %s gets into %s, driver %s (can't be dragged %d)", to_string(m_pedId).c_str(),
        to_string(syncer).c_str(), to_string(m_enterVehicleId).c_str(),
        pDriverPlayer ? to_string(pDriverPlayer->m_playerId).c_str() : vehicle->m_pDriver ? "a ped" : "none",
        vehicle->m_pDriver ? vehicle->m_pDriver->m_nPedFlags.bDontDragMeOutCar : -1);

    CTask* pTask = bPassenger ? static_cast<CTask*>(new CTaskComplexEnterCarAsPassenger(vehicle, seat, false))
                              : static_cast<CTask*>(new CTaskComplexEnterCarAsDriver(vehicle));
    m_pPed->m_pIntelligence->m_TaskMgr.SetTask(pTask, TASK_PRIMARY_PRIMARY, false);
}

bool CNetworkPed::IsEnteringVehicle(CVehicle* vehicle)
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

bool CNetworkPed::HasJustEnteredVehicle()
{
    if (m_pPed == nullptr || m_nEnterVehicleTime == 0 ||
        CTimer::m_snTimeInMilliseconds - m_nEnterVehicleTime > ENTER_VEHICLE_TIMEOUT)
    {
        return false;
    }

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(m_enterVehicleId);
    return pNetworkVehicle && m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle == pNetworkVehicle->m_pVehicle;
}

static CTaskComplexEnterCar* FindEnterTask(CPed* pPed)
{
    auto& taskManager = pPed->m_pIntelligence->m_TaskMgr;
    for (int taskType : {TASK_COMPLEX_ENTER_CAR_AS_PASSENGER, TASK_COMPLEX_ENTER_CAR_AS_DRIVER})
    {
        if (auto pTask = static_cast<CTaskComplexEnterCar*>(taskManager.FindActiveTaskByType(taskType)))
        {
            return pTask;
        }
    }
    return nullptr;
}

bool CNetworkPed::IsGettingIntoVehicle()
{
    return m_pPed && !m_pPed->m_nPedFlags.bInVehicle && FindEnterTask(m_pPed);
}

void CNetworkPed::CollectVehicleEnter()
{
    CTaskComplexEnterCar* pTask = FindEnterTask(m_pPed);

    CVehicle* pVehicle = pTask && !m_pPed->m_nPedFlags.bInVehicle ? pTask->m_pTargetVehicle : nullptr;
    if (pVehicle == m_pEnterSentVehicle)
    {
        return;
    }
    m_pEnterSentVehicle = pVehicle;

    CNetworkVehicle* pNetworkVehicle = pVehicle ? CNetworkVehicleManager::GetVehicle(pVehicle) : nullptr;
    if (!pNetworkVehicle)
    {
        return;
    }

    Packets::Peds::PedVehicleEnter packet{};
    packet.pedid = m_pedId;
    packet.vehicleid = pNetworkVehicle->m_vehicleId;
    packet.passenger = pTask->GetTaskType() == TASK_COMPLEX_ENTER_CAR_AS_PASSENGER;
    packet.seat = static_cast<int8_t>(std::clamp(pTask->m_nTargetSeat, 0, 7));
    GetPacketFactory().Send(packet);
}

void CNetworkPed::ClaimOnRelease()
{
    if (m_bClaimOnRelease || m_bSyncing)
        return;

    Packets::Peds::PedClaimOnRelease packet{};
    packet.pedid = m_pedId;
    GetPacketFactory().Send(packet);

    m_bClaimOnRelease = true;
}

void CNetworkPed::CancelClaim()
{
    if (!m_bClaimOnRelease || m_bSyncing)
        return;

    Packets::Peds::PedCancelClaim packet{};
    packet.pedid = m_pedId;
    GetPacketFactory().Send(packet);

    m_bClaimOnRelease = false;
}

void CNetworkPed::ApplyWeaponSnapshot(Packets::Players::SWeaponSnapshot& weaponSnapshot)
{
    if (m_pPed == nullptr)
    {
        return;
    }

    // TODO refactor CUtil
    CUtil::GiveWeaponByPacket(this, weaponSnapshot.iWeaponType, weaponSnapshot.nAmmo);
    m_pPed->m_aWeapons[m_pPed->m_nActiveWeaponSlot].m_nState = static_cast<eWeaponState>(weaponSnapshot.iWeaponState);
}

void CNetworkPed::BecomeSyncer(bool bResumeAmbientLife)
{
    m_bSyncing = true;
    m_bClaimOnRelease = false;
    // our physics moves it from now on
    if (m_bFrozen && m_pPed)
    {
        m_pPed->m_nPhysicalFlags.bDontApplySpeed = false;
    }
    m_bFrozen = false;

    // the clone's wait for a leave of the old syncer is over; our own leave is watched from now on
    m_pendingExit.reset();
    m_nLeaveVehicleTime = 0;
    m_exitWatchVehicleId = vehicleid_t();
    m_bExitLeaveSent = false;
    m_nOwnershipChangeTime = CTimer::m_snTimeInMilliseconds;

    if (!m_pPed)
    {
        return;
    }

    CPedAnimSync::Release(m_pPed);

    m_pPed->SetCharCreatedBy(m_nCreatedBy);
    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 0;  // 0 - default

    if (!bResumeAmbientLife || m_nCreatedBy != RANDOM_CHAR)
    {
        return;
    }

    // the clone constructor cut the perception, SetCharCreatedBy doesn't restore it
    // (defaults from the CPedIntelligence constructor 0x607140)
    bool bGang = m_pPed->m_nPedType >= PED_TYPE_GANG1 && m_pPed->m_nPedType <= PED_TYPE_GANG10;
    m_pPed->m_pIntelligence->SetSeeingRange(bGang ? 40.0f : 15.0f);
    m_pPed->m_pIntelligence->SetHearingRange(bGang ? 40.0f : 15.0f);
    m_pPed->m_pIntelligence->m_fDmRadius = 15.0f;
    m_pPed->m_pIntelligence->m_nDmNumPedsToScan = 3;

    // the population code may remove it again, but not in the same frame (m_nTimeTillWeNeedThisPed)
    m_pPed->field_54C = static_cast<int>(CTimer::m_snTimeInMilliseconds + 5000);

    if (m_pPed->m_nPedFlags.bInVehicle)
    {
        // the vehicle handoff may have come first
        if (CNetworkVehicle* pNetworkVehicle = m_pPed->m_pVehicle ? CNetworkVehicleManager::GetVehicle(m_pPed->m_pVehicle) : nullptr)
        {
            pNetworkVehicle->ResumeDriving();
        }
    }
    else if (CPedGroup* pGroup = CPedGroups::GetPedsGroup(m_pPed); pGroup && pGroup->m_groupMembership.GetLeader() &&
                                                                pGroup->m_groupMembership.GetLeader()->IsPlayer())
    {
        // a follower of our player (CPopulationHandover claims them): the group gives its tasks through the primary
        // CTaskComplexBeInGroup, what a ped joining a group gets (see CPedIntelligence::FlushImmediately)
        CTask* pBeInGroup = static_cast<CTask*>(CTask::operator new(0x28));
        plugin::CallMethod<0x632E50, CTask*, int, bool>(pBeInGroup, CPedGroups::GetGroupId(pGroup), false);
        m_pPed->m_pIntelligence->m_TaskMgr.SetTask(pBeInGroup, TASK_PRIMARY_PRIMARY, false);
    }
    else
    {
        // CTaskComplexWander::GetWanderTaskByPedType, the same task CPopulation::AddPed gives
        CTask* pWanderTask = plugin::CallAndReturn<CTask*, 0x673D00, CPed*>(m_pPed);
        m_pPed->m_pIntelligence->m_TaskMgr.SetTask(pWanderTask, TASK_PRIMARY_DEFAULT, false);
    }
}

void CNetworkPed::BecomeClone(bool bStopAmbientLife)
{
    m_bSyncing = false;
    m_bBorrowed = false;
    m_nOwnershipChangeTime = CTimer::m_snTimeInMilliseconds;
    m_bHasTargetPos = false;

    if (!m_pPed)
    {
        return;
    }

    m_pPed->SetCharCreatedBy(MISSION_CHAR);
    // its owner says when it falls off (a clone in a vehicle taken over keeps the flag of its old owner)
    m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1;  // 1 - never

    if (!bStopAmbientLife)
    {
        return;
    }

    // start from where the ped is now, not from the last packet received before we owned it
    m_vecVelocity = m_pPed->m_vecMoveSpeed;
    m_fCurrentRotation = m_pPed->m_fCurrentRotation;
    m_fAimingRotation = m_pPed->m_fAimingRotation;
    m_fHealth = m_pPed->m_fHealth;

    if (m_pPed->m_nPedFlags.bInVehicle)
    {
        // flushing the driver tasks would throw it out, the vehicle ai hooks stop it for clones anyway
        if (CVehicle* pVehicle = m_pPed->m_pVehicle)
        {
            m_fGasPedal = pVehicle->m_fGasPedal;
            m_fBreakPedal = pVehicle->m_fBreakPedal;
            m_fSteerAngle = pVehicle->m_fSteerAngle;
        }
        return;
    }

    m_pPed->m_pIntelligence->FlushImmediately(false);
}

void CNetworkPed::ApplyHandoff(const Packets::Peds::PedHandoff& handoff)
{
    if (m_pPed && handoff.hasTransform)
    {
        m_pPed->SetPosn(handoff.pos);
        m_pPed->m_fCurrentRotation = m_pPed->m_fAimingRotation = handoff.heading;
        m_pPed->SetHeading(handoff.heading);
    }

    BecomeSyncer(true);

    // mission peds are handed over only by the collision rule of CPopulationHandover::Process
    m_bBorrowed = m_nCreatedBy == MISSION_CHAR && !CLocalPlayer::m_bIsHost;
    logger::info("ped %s taken over", to_string(handoff.pedid).c_str());
}

// the clone walks on its own synced anims, the packets only correct the drift: setting the packet position every
// 50 ms pulled it back to where the syncer's ped was a moment ago, it moved in small jumps
static constexpr float TARGET_SNAP_DISTANCE = 3.0f;
static constexpr unsigned int MAX_TARGET_EXTRAPOLATION = 250;  // ms, a lost packet must not send it flying

void CNetworkPed::SetTargetPosition(const CVector& pos, const CVector& velocity)
{
    m_vecTargetPos = pos;
    m_vecVelocity = velocity;
    m_nTargetPosTime = CTimer::m_snTimeInMilliseconds;

    if (!m_bHasTargetPos || DistanceBetweenPoints(m_pPed->GetPosition(), pos) > TARGET_SNAP_DISTANCE)
    {
        m_pPed->SetPosn(pos);
    }
    m_bHasTargetPos = true;
}

void CNetworkPed::ProcessTargetPosition()
{
    if (!m_bHasTargetPos || !m_pPed)
    {
        return;
    }

    // where the syncer's ped is now: the last position moved on by its speed (m_vecMoveSpeed is per 1/50 s)
    unsigned int nElapsed = std::min(CTimer::m_snTimeInMilliseconds - m_nTargetPosTime, MAX_TARGET_EXTRAPOLATION);
    CVector predicted = m_vecTargetPos + m_vecVelocity * (nElapsed / 20.0f);
    CVector error = predicted - m_pPed->GetPosition();
    if (error.Magnitude() > TARGET_SNAP_DISTANCE)
    {
        m_pPed->SetPosn(predicted);
        return;
    }

    // frozen its own walk doesn't move it, the pull is all it moves by: close behind the prediction
    float fPull = std::min(1.0f, (m_bFrozen ? 0.5f : 0.1f) * CTimer::ms_fTimeStep);
    m_pPed->SetPosn(m_pPed->GetPosition() + error * fPull);
}

void CNetworkPed::ProcessFreeze()
{
    // pushed by vehicles and peds of this game, the clone was a few cm off its owner's ped every hit and the pull dragged
    // it back (neon's UpdateRemoteReplicaPhysicsFence): on foot its physics doesn't move it, the position sync does; a
    // hit still pushes the vehicle back by the ped's mass. Its own tasks keep the physics: our copies of an enter,
    // a leave, a fall, a death, a stealth kill
    bool bFreeze = ms_bFreezeClones && !m_bSyncing && m_pPed && m_pPed->IsAlive() && !m_pPed->m_nPedFlags.bInVehicle && m_bHasTargetPos &&
                   !m_pPed->m_pAttachedTo;
    if (bFreeze)
    {
        auto& taskManager = m_pPed->m_pIntelligence->m_TaskMgr;
        for (int i = TASK_PRIMARY_PHYSICAL_RESPONSE; i <= TASK_PRIMARY_PRIMARY && bFreeze; i++)
        {
            bFreeze = taskManager.m_aPrimaryTasks[i] == nullptr;
        }
    }

    if (bFreeze != m_bFrozen && m_pPed)
    {
        m_pPed->m_nPhysicalFlags.bDontApplySpeed = bFreeze;
        if (bFreeze)
        {
            m_pPed->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
        }
    }
    m_bFrozen = bFreeze;
}
