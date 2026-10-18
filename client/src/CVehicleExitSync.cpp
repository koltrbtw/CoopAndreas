#include "stdafx.h"
#include "CPedAnimSync.h"
#include <CAnimManager.h>
#include <CCarEnterExit.h>
#include <CAnimBlendStaticAssociation.h>

using namespace Packets::Vehicles;

static constexpr uint16_t ANIM_FLAG_LOOPED = 0x2;

// CTaskComplexFallAndGetUp (gta-reversed TaskComplexFallAndGetUp.h, 0x18 bytes)
static constexpr unsigned int FALL_AND_GET_UP_SIZE = 0x18;
static constexpr unsigned int FALL_AND_GET_UP_ANIM_ID = 0xC;
static constexpr unsigned int FALL_AND_GET_UP_ANIM_GROUP = 0x10;
static constexpr unsigned int FALL_AND_GET_UP_DOWN_TIME = 0x14;
// CTaskComplexInAirAndLand, 0x10 bytes
static constexpr unsigned int IN_AIR_AND_LAND_SIZE = 0x10;
// CTaskSimpleCarJumpOut::m_anim, the roll out anim
static constexpr unsigned int CAR_JUMP_OUT_ANIM = 0xC;

template <typename T>
static T TaskField(CTask* pTask, unsigned int nOffset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(pTask) + nOffset);
}

template <typename TPacket>
static void SetFallAnim(TPacket& packet, int nGroup, int nAnimId, int nDownTime)
{
    packet.fall = VEHICLE_EXIT_FALL_ANIM;
    packet.animGroup = static_cast<uint8_t>(nGroup);
    packet.animId = static_cast<uint16_t>(nAnimId);
    packet.downTime = static_cast<uint16_t>(std::clamp(nDownTime, 0, 0xFFFF));
}

// the tasks the frame the vehicle let the ped go (VEHICLE_EXIT, PED_VEHICLE_EXIT): the event responses are set by then
template <typename TPacket>
static void CollectReason(CPed* pPlayerPed, bool bLeaveSent, TPacket& packet)
{
    auto& taskManager = pPlayerPed->m_pIntelligence->m_TaskMgr;

    if (CTask* pFall = taskManager.FindActiveTaskByType(TASK_COMPLEX_FALL_AND_GET_UP))
    {
        if (pPlayerPed == FindPlayerPed(0))
        {
            CPlayerFallSync::OnFallSent(pFall);
        }
        SetFallAnim(packet, TaskField<int>(pFall, FALL_AND_GET_UP_ANIM_GROUP), TaskField<int>(pFall, FALL_AND_GET_UP_ANIM_ID),
            TaskField<int>(pFall, FALL_AND_GET_UP_DOWN_TIME));
    }
    else if (taskManager.FindActiveTaskByType(TASK_COMPLEX_IN_AIR_AND_LAND))
    {
        packet.fall = VEHICLE_EXIT_FALL_IN_AIR;
    }

    if (pPlayerPed->m_fHealth < 1.0f || !pPlayerPed->IsAlive())
    {
        packet.reason = VEHICLE_EXIT_DIED;
        packet.fall = VEHICLE_EXIT_FALL_NONE;  // PED_DIED brings the death, a get up would revive the clone
    }
    else if (CTask* pJumpOut = taskManager.FindActiveTaskByType(TASK_SIMPLE_CAR_JUMP_OUT))
    {
        packet.reason = VEHICLE_EXIT_JUMP_OUT;
        // the roll goes on after the vehicle let it go, then the ped gets up (CTaskComplexLeaveCar)
        if (auto pAnim = TaskField<CAnimBlendAssociation*>(pJumpOut, CAR_JUMP_OUT_ANIM))
        {
            SetFallAnim(packet, pAnim->m_nAnimGroup, pAnim->m_nAnimId, 0);
        }
    }
    else if (taskManager.FindActiveTaskByType(TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT_AND_STAND_UP) ||
             taskManager.FindActiveTaskByType(TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT) ||
             taskManager.FindActiveTaskByType(TASK_COMPLEX_CAR_QUICK_BE_DRAGGED_OUT) ||
             taskManager.FindActiveTaskByType(TASK_SIMPLE_BIKE_JACKED))
    {
        packet.reason = VEHICLE_EXIT_DRAGGED;
        // the drag out anim has ended by now, the ped lies on its back and gets up (CTaskSimpleGetUp: FLOOR_HIT, GETUP)
        if (packet.fall == VEHICLE_EXIT_FALL_NONE)
        {
            SetFallAnim(packet, ANIM_GROUP_DEFAULT, ANIM_DEFAULT_FLOOR_HIT, 0);
        }
    }
    else if (pPlayerPed->m_nPedFlags.bKnockedOffBike || packet.fall != VEHICLE_EXIT_FALL_NONE)
    {
        packet.reason = VEHICLE_EXIT_KNOCKED_OFF;
    }
    else if (bLeaveSent || taskManager.FindActiveTaskByType(TASK_COMPLEX_LEAVE_CAR))
    {
        // the leave task may finish the frame it sets the ped out
        packet.reason = VEHICLE_EXIT_NORMAL;
    }
    else
    {
        packet.reason = VEHICLE_EXIT_WARP;
    }
}

void CVehicleExitSync::SendOut(CPlayerPed* pPlayerPed, vehicleid_t vehicleid)
{
    VehicleExit packet{};
    packet.vehicleid = vehicleid;
    packet.pos = pPlayerPed->GetPosition();
    packet.moveSpeed = pPlayerPed->m_vecMoveSpeed;
    packet.heading = pPlayerPed->m_fCurrentRotation;
    CollectReason(pPlayerPed, ms_bLeaveSent, packet);
    GetPacketFactory().Send(packet);

    logger::info("vehicle exit %s: reason %d fall %d anim %d/%d down %d", to_string(vehicleid).c_str(), packet.reason,
        packet.fall, packet.animGroup, packet.animId, packet.downTime);
}

void CVehicleExitSync::SendLeave(CVehicle* pVehicle)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle);
    if (!pNetworkVehicle)
    {
        return;
    }

    VehicleExit packet{};
    packet.vehicleid = pNetworkVehicle->m_vehicleId;
    packet.reason = VEHICLE_EXIT_LEAVE;
    GetPacketFactory().Send(packet);
    ms_bLeaveSent = true;
}

void CVehicleExitSync::Process()
{
    CPlayerPed* pPlayerPed = FindPlayerPed(0);
    if (!pPlayerPed)
    {
        return;
    }

    // a carriage behind the front of a train has no network id, it counts as out
    CVehicle* pVehicle = pPlayerPed->m_nPedFlags.bInVehicle ? pPlayerPed->m_pVehicle : nullptr;
    CNetworkVehicle* pNetworkVehicle = pVehicle ? CNetworkVehicleManager::GetVehicle(pVehicle) : nullptr;
    vehicleid_t vehicleid = pNetworkVehicle ? pNetworkVehicle->m_vehicleId : vehicleid_t();

    // before this frame's on foot update: the clone has to know how it got out before it is moved
    if (ms_vehicleId.valid() && ms_vehicleId != vehicleid)
    {
        SendOut(pPlayerPed, ms_vehicleId);
    }
    if (ms_vehicleId != vehicleid)
    {
        ms_bLeaveSent = false;
    }
    ms_vehicleId = vehicleid;

    // a script's leave / enter task of ours (a mission's TASK_LEAVE_ANY_CAR, TASK_ENTER_CAR_AS_PASSENGER replayed for
    // us): only the button's are hooked, the clones got a warp out and a warp in after the scene
    auto& taskManager = pPlayerPed->m_pIntelligence->m_TaskMgr;
    if (vehicleid.valid() && !ms_bLeaveSent)
    {
        auto pLeave = static_cast<CTaskComplexLeaveCar*>(taskManager.FindActiveTaskByType(TASK_COMPLEX_LEAVE_CAR));
        if (pLeave && pLeave->m_pTargetVehicle == pVehicle)
        {
            SendLeave(pVehicle);
        }
    }

    CTaskComplexEnterCar* pEnter = nullptr;
    for (int taskType : {TASK_COMPLEX_ENTER_CAR_AS_PASSENGER, TASK_COMPLEX_ENTER_CAR_AS_DRIVER})
    {
        if (!pEnter)
        {
            pEnter = static_cast<CTaskComplexEnterCar*>(taskManager.FindActiveTaskByType(taskType));
        }
    }
    CVehicle* pEnterVehicle = pEnter && !pVehicle ? pEnter->m_pTargetVehicle : nullptr;
    if (pEnterVehicle && pEnterVehicle != ms_pEnterSentVehicle)
    {
        if (CNetworkVehicle* pNetworkEnter = CNetworkVehicleManager::GetVehicle(pEnterVehicle))
        {
            bool bPassenger = pEnter->GetTaskType() == TASK_COMPLEX_ENTER_CAR_AS_PASSENGER;
            // a passenger's target is a door id (CCarEnterExit), the packet takes the passenger index
            int nDoor = pEnter->m_nTargetDoor ? pEnter->m_nTargetDoor : pEnter->m_nTargetSeat;
            Packets::Vehicles::VehicleEnter packet{};
            packet.vehicleid = pNetworkEnter->m_vehicleId;
            packet.bPassenger = bPassenger;
            packet.seatid = bPassenger ? static_cast<int8_t>(std::clamp(
                                             CCarEnterExit::ComputePassengerIndexFromCarDoor(pEnterVehicle, nDoor), 0, 7))
                                       : 0;
            packet.bWalk = pEnter->m_nMoveState == PEDMOVE_WALK;
            GetPacketFactory().Send(packet);
            logger::info("our enter task %d into %s (seat %d, door %d, target seat %d) sent", pEnter->GetTaskType(),
                to_string(packet.vehicleid).c_str(), packet.seatid, pEnter->m_nTargetDoor, pEnter->m_nTargetSeat);
        }
    }
    // kept while the task switches its subtasks (a frame with no enter task), forgotten once in a vehicle
    if (pVehicle)
    {
        ms_pEnterSentVehicle = nullptr;
    }
    else if (pEnterVehicle)
    {
        ms_pEnterSentVehicle = pEnterVehicle;
    }
}

void CVehicleExitSync::Reset()
{
    ms_vehicleId = vehicleid_t();
    ms_bLeaveSent = false;
}

void CVehicleExitSync::ApplyFall(CPed* pPed, uint8_t fall, int nGroup, int nAnimId, int nDownTime)
{
    auto& taskManager = pPed->m_pIntelligence->m_TaskMgr;

    // its own jump out task got it out the same way, a jacker here dragged it out and it gets up
    if (taskManager.FindActiveTaskByType(TASK_COMPLEX_FALL_AND_GET_UP) ||
        taskManager.FindActiveTaskByType(TASK_COMPLEX_IN_AIR_AND_LAND) ||
        taskManager.FindActiveTaskByType(TASK_SIMPLE_CAR_JUMP_OUT) || taskManager.FindActiveTaskByType(TASK_SIMPLE_GET_UP) ||
        taskManager.FindActiveTaskByType(TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT_AND_STAND_UP))
    {
        return;
    }

    CTask* pTask = nullptr;
    if (fall == VEHICLE_EXIT_FALL_IN_AIR)
    {
        pTask = static_cast<CTask*>(CTask::operator new(IN_AIR_AND_LAND_SIZE));
        plugin::CallMethod<0x678C80, CTask*, bool, bool>(pTask, false, false);  // CTaskComplexInAirAndLand ctor
    }
    else if (fall == VEHICLE_EXIT_FALL_ANIM)
    {
        // a bike's own block may be gone here already, ped.ifp is always in; CTaskSimpleFall waits for the end of
        // its anim, a looped one would keep the ped down for good
        CAnimBlendStaticAssociation* pAnim =
            CPedAnimSync::IsGroupLoaded(nGroup) ? CAnimManager::GetAnimAssociation(nGroup, nAnimId) : nullptr;
        if (!pAnim || (pAnim->m_nFlags & ANIM_FLAG_LOOPED))
        {
            nGroup = ANIM_GROUP_DEFAULT;
            nAnimId = ANIM_DEFAULT_KO_SKID_BACK;
        }

        pTask = static_cast<CTask*>(CTask::operator new(FALL_AND_GET_UP_SIZE));
        plugin::CallMethod<0x6786C0, CTask*, int, int, int>(pTask, nAnimId, nGroup, nDownTime);  // CTaskComplexFallAndGetUp ctor
    }

    if (pTask)
    {
        taskManager.SetTask(pTask, TASK_PRIMARY_PHYSICAL_RESPONSE, false);
    }
}

// out: from the sender's frame the vehicle let it go
static void ApplyExit(CNetworkPlayer* pNetworkPlayer, const VehicleExit& packet)
{
    CPlayerPed* pPlayerPed = pNetworkPlayer->m_pPed;
    CVehicle* pVehicle = pPlayerPed->m_nPedFlags.bInVehicle ? pPlayerPed->m_pVehicle : nullptr;
    logger::info("%s out of %s (reason %d fall %d): %s", pNetworkPlayer->GetName().c_str(),
        to_string(packet.vehicleid).c_str(), packet.reason, packet.fall, pVehicle ? "warped out" : "already out");

    pNetworkPlayer->m_nLeaveVehicleTime = 0;
    pNetworkPlayer->m_pendingExit.reset();

    // an on foot update that came first may have got it out already
    if (pVehicle)
    {
        pNetworkPlayer->RemoveFromVehicle(pVehicle, packet.fall != VEHICLE_EXIT_FALL_NONE);
    }

    pPlayerPed->SetPosn(packet.pos);
    pPlayerPed->m_vecMoveSpeed = packet.moveSpeed;
    pPlayerPed->m_fCurrentRotation = pPlayerPed->m_fAimingRotation = packet.heading.m_angle;
    pPlayerPed->SetHeading(packet.heading.m_angle);

    CVehicleExitSync::ApplyFall(pPlayerPed, packet.fall, packet.animGroup, packet.animId, packet.downTime);
}

bool CVehicleExitSync::ApplyPendingExit(CNetworkPlayer* pNetworkPlayer)
{
    if (!pNetworkPlayer->m_pendingExit || !pNetworkPlayer->m_pPed)
    {
        return false;
    }

    VehicleExit packet = *pNetworkPlayer->m_pendingExit;
    ApplyExit(pNetworkPlayer, packet);
    return true;
}

// a ped clone here plays the enter task of its syncer into the vehicle the player sits in (PED_VEHICLE_ENTER)
static bool IsBeingJackedHere(CNetworkPlayer* pNetworkPlayer)
{
    CPlayerPed* pPlayerPed = pNetworkPlayer->m_pPed;
    if (!pPlayerPed || !pPlayerPed->m_nPedFlags.bInVehicle || !pPlayerPed->m_pVehicle)
    {
        return false;
    }

    for (CNetworkPed* pNetworkPed : CNetworkPedManager::m_pPeds)
    {
        if (!pNetworkPed->m_bSyncing && pNetworkPed->IsEnteringVehicle(pPlayerPed->m_pVehicle))
        {
            return true;
        }
    }
    return false;
}

void CVehicleExitSync::ProcessClones()
{
    // ped clones: their exit waited for our copy of their leave, or of a player jacking them
    for (CNetworkPed* pNetworkPed : CNetworkPedManager::m_pPeds)
    {
        CPed* pPed = pNetworkPed->m_pPed;
        if (pNetworkPed->m_bSyncing || !pPed || !pNetworkPed->m_pendingExit)
        {
            continue;
        }

        CVehicle* pVehicle = pPed->m_nPedFlags.bInVehicle ? pPed->m_pVehicle : nullptr;
        if (!pVehicle)
        {
            pNetworkPed->m_pendingExit.reset();
        }
        else if (!pNetworkPed->IsLeavingVehicle(pVehicle))
        {
            ApplyPendingPedExit(pNetworkPed);
        }
    }

    for (CNetworkPlayer* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        CPlayerPed* pPlayerPed = pNetworkPlayer->m_pPed;
        if (!pPlayerPed)
        {
            continue;
        }

        // our copy of the jacker gave up (no room at the door here, its own path): no reason to wait for it
        auto& taskManager = pPlayerPed->m_pIntelligence->m_TaskMgr;
        bool bDragged = taskManager.FindActiveTaskByType(TASK_SIMPLE_CAR_SLOW_BE_DRAGGED_OUT) ||
                        taskManager.FindActiveTaskByType(TASK_COMPLEX_CAR_SLOW_BE_DRAGGED_OUT_AND_STAND_UP);
        if (pNetworkPlayer->IsJackAllowed() && pPlayerPed->m_nPedFlags.bInVehicle && !bDragged &&
            !IsBeingJackedHere(pNetworkPlayer))
        {
            logger::info("%s: our copy of its jacker gave up", pNetworkPlayer->GetName().c_str());
            pNetworkPlayer->m_nJackAllowedUntil = 0;
        }

        // the sender's exit waited for our own leave or jack; it got out by it, or that gave up
        if (pNetworkPlayer->m_pendingExit)
        {
            CVehicle* pVehicle = pPlayerPed->m_nPedFlags.bInVehicle ? pPlayerPed->m_pVehicle : nullptr;
            if (!pVehicle)
            {
                pNetworkPlayer->m_pendingExit.reset();
            }
            else if (!pNetworkPlayer->IsLeavingVehicle(pVehicle))
            {
                ApplyPendingExit(pNetworkPlayer);
            }
        }
    }
}

void CVehicleExitSync::HandlePacket(const VehicleExit& packet)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(packet.senderPlayerid);
    if (!pNetworkPlayer || !pNetworkPlayer->m_pPed || !pNetworkPlayer->m_pPed->IsVTableValid())
    {
        return;
    }

    pNetworkPlayer->m_nEnterVehicleTime = 0;

    CPlayerPed* pPlayerPed = pNetworkPlayer->m_pPed;
    CVehicle* pVehicle = pPlayerPed->m_nPedFlags.bInVehicle ? pPlayerPed->m_pVehicle : nullptr;
    if (pVehicle && !pVehicle->IsVTableValid())
    {
        return;
    }

    if (packet.reason == VEHICLE_EXIT_LEAVE)
    {
        if (pVehicle)
        {
            pNetworkPlayer->LeaveVehicle(pVehicle);
        }
        return;
    }

    pNetworkPlayer->m_exitVehicleId = packet.vehicleid;
    pNetworkPlayer->m_nExitVehicleTime = CTimer::m_snTimeInMilliseconds;

    // our copy of the leave task or of a jacker gets it out on its own, a warp would cut the anim; it waits until
    // that is over (ProcessClones). A thrown out sender is out at once, our leave task would take seconds more
    if (pVehicle && pNetworkPlayer->IsLeavingVehicle(pVehicle) &&
        (packet.fall == VEHICLE_EXIT_FALL_NONE || packet.reason == VEHICLE_EXIT_DRAGGED))
    {
        logger::info("%s out of %s (reason %d): waits for its own leave here", pNetworkPlayer->GetName().c_str(),
            to_string(packet.vehicleid).c_str(), packet.reason);
        pNetworkPlayer->m_pendingExit = packet;
        return;
    }

    // our copy of the jacker got it out already, the fall is played
    if (!pVehicle && packet.reason == VEHICLE_EXIT_DRAGGED && pNetworkPlayer->IsJackAllowed())
    {
        logger::info("%s out of %s (reason %d): dragged out here", pNetworkPlayer->GetName().c_str(),
            to_string(packet.vehicleid).c_str(), packet.reason);
        return;
    }

    ApplyExit(pNetworkPlayer, packet);
}

// out: from the syncer's frame the vehicle let its ped go
static void ApplyPedExit(CNetworkPed* pNetworkPed, const Packets::Peds::PedVehicleExit& packet)
{
    CPed* pPed = pNetworkPed->m_pPed;
    CVehicle* pVehicle = pPed->m_nPedFlags.bInVehicle ? pPed->m_pVehicle : nullptr;
    logger::info("ped %s out of %s (reason %d fall %d): %s", to_string(packet.pedid).c_str(),
        to_string(packet.vehicleid).c_str(), packet.reason, packet.fall, pVehicle ? "warped out" : "already out");

    pNetworkPed->m_nLeaveVehicleTime = 0;
    pNetworkPed->m_pendingExit.reset();

    // an on foot update that came first may have got it out already
    if (pVehicle)
    {
        pNetworkPed->RemoveFromVehicle(pVehicle, packet.fall != VEHICLE_EXIT_FALL_NONE);
    }

    // CNetworkPedManager::Process turns the clone to these every frame
    pNetworkPed->m_fCurrentRotation = pNetworkPed->m_fAimingRotation = packet.heading.m_angle;
    pPed->m_fCurrentRotation = pPed->m_fAimingRotation = packet.heading.m_angle;
    pPed->SetHeading(packet.heading.m_angle);
    pNetworkPed->m_bHasTargetPos = false;
    pNetworkPed->SetTargetPosition(packet.pos, packet.moveSpeed);
    pPed->m_vecMoveSpeed = packet.moveSpeed;

    CVehicleExitSync::ApplyFall(pPed, packet.fall, packet.animGroup, packet.animId, packet.downTime);
}

bool CVehicleExitSync::ApplyPendingPedExit(CNetworkPed* pNetworkPed)
{
    if (!pNetworkPed->m_pendingExit || !pNetworkPed->m_pPed)
    {
        return false;
    }

    Packets::Peds::PedVehicleExit packet = *pNetworkPed->m_pendingExit;
    ApplyPedExit(pNetworkPed, packet);
    return true;
}

void CVehicleExitSync::ProcessPed(CNetworkPed* pNetworkPed)
{
    CPed* pPed = pNetworkPed->m_pPed;
    CVehicle* pVehicle = pPed->m_nPedFlags.bInVehicle ? pPed->m_pVehicle : nullptr;
    CNetworkVehicle* pNetworkVehicle = pVehicle ? CNetworkVehicleManager::GetVehicle(pVehicle) : nullptr;
    vehicleid_t vehicleid = pNetworkVehicle ? pNetworkVehicle->m_vehicleId : vehicleid_t();

    // before this update's on foot one: the clone has to know how it got out before it is moved
    if (pNetworkPed->m_exitWatchVehicleId.valid() && pNetworkPed->m_exitWatchVehicleId != vehicleid)
    {
        Packets::Peds::PedVehicleExit packet{};
        packet.pedid = pNetworkPed->m_pedId;
        packet.vehicleid = pNetworkPed->m_exitWatchVehicleId;
        packet.pos = pPed->GetPosition();
        packet.moveSpeed = pPed->m_vecMoveSpeed;
        packet.heading = pPed->m_fCurrentRotation;
        CollectReason(pPed, pNetworkPed->m_bExitLeaveSent, packet);
        GetPacketFactory().Send(packet);
        logger::info("ped %s out of %s: reason %d fall %d anim %d/%d", to_string(packet.pedid).c_str(),
            to_string(packet.vehicleid).c_str(), packet.reason, packet.fall, packet.animGroup, packet.animId);
    }
    if (pNetworkPed->m_exitWatchVehicleId != vehicleid)
    {
        pNetworkPed->m_bExitLeaveSent = false;
    }
    pNetworkPed->m_exitWatchVehicleId = vehicleid;

    // its leave task started (its ai: a car on fire, upside down, someone got in, a script): the clones leave too.
    // Not a dead one: DieInCar may leave the car with the body, PED_DIED brings the death
    if (vehicleid.valid() && !pNetworkPed->m_bExitLeaveSent && pPed->IsAlive())
    {
        auto pLeave = static_cast<CTaskComplexLeaveCar*>(
            pPed->m_pIntelligence->m_TaskMgr.FindActiveTaskByType(TASK_COMPLEX_LEAVE_CAR));
        if (pLeave && pLeave->m_pTargetVehicle == pVehicle)
        {
            Packets::Peds::PedVehicleExit packet{};
            packet.pedid = pNetworkPed->m_pedId;
            packet.vehicleid = vehicleid;
            packet.reason = VEHICLE_EXIT_LEAVE;
            GetPacketFactory().Send(packet);
            pNetworkPed->m_bExitLeaveSent = true;
        }
    }
}

void CVehicleExitSync::HandlePedPacket(const Packets::Peds::PedVehicleExit& packet)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(packet.pedid);
    CPed* pPed = pNetworkPed ? pNetworkPed->m_pPed : nullptr;
    if (!pPed || pNetworkPed->m_bSyncing || !pPed->IsVTableValid())
    {
        return;
    }

    CVehicle* pVehicle = pPed->m_nPedFlags.bInVehicle ? pPed->m_pVehicle : nullptr;
    if (pVehicle && !pVehicle->IsVTableValid())
    {
        return;
    }

    if (packet.reason == VEHICLE_EXIT_LEAVE)
    {
        if (pVehicle && pPed->IsAlive())
        {
            pNetworkPed->LeaveVehicle(pVehicle);
        }
        return;
    }

    pNetworkPed->m_exitVehicleId = packet.vehicleid;
    pNetworkPed->m_nExitVehicleTime = CTimer::m_snTimeInMilliseconds;

    // our copy of the leave task or of a jacker gets it out on its own, it waits until that is over (ProcessClones);
    // a thrown out ped is out at once, our leave task would take seconds more
    if (pVehicle && pNetworkPed->IsLeavingVehicle(pVehicle) &&
        (packet.fall == VEHICLE_EXIT_FALL_NONE || packet.reason == VEHICLE_EXIT_DRAGGED))
    {
        pNetworkPed->m_pendingExit = packet;
        return;
    }

    ApplyPedExit(pNetworkPed, packet);
}
