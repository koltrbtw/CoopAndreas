#include "stdafx.h"
#include "CPedStateSync.h"
#include <CFireManager.h>

// sizes and ctors (gta-reversed, the allocations at their callers): CTaskComplexInWater 0xC, CTaskSimpleChoking 0x1C,
// CTaskSimpleClimb 0x30
static constexpr unsigned int IN_WATER_SIZE = 0xC;
static constexpr unsigned int CHOKING_SIZE = 0x1C;
static constexpr unsigned int CLIMB_SIZE = 0x30;
static constexpr int8_t CLIMB_HEIGHT_GRAB = 1;  // eClimbHeights::CLIMB_GRAB
static constexpr unsigned int CLONE_FIRE_TIME = 7000;  // ms, lit again while the owner's ped burns

static bool HasTask(CPed* pPed, int nTaskType)
{
    return pPed->m_pIntelligence->m_TaskMgr.FindActiveTaskByType(nTaskType) != nullptr;
}

bool CPedStateSync::IsClimbing(CPed* pPed)
{
    return HasTask(pPed, TASK_SIMPLE_CLIMB);
}

uint8_t CPedStateSync::Collect(CPed* pPed)
{
    uint8_t flags = 0;
    flags |= pPed->m_pFire ? STATE_ON_FIRE : 0;
    flags |= HasTask(pPed, TASK_SIMPLE_SWIM) ? STATE_SWIMMING : 0;
    flags |= HasTask(pPed, TASK_SIMPLE_CHOKING) ? STATE_CHOKING : 0;
    flags |= HasTask(pPed, TASK_SIMPLE_CLIMB) ? STATE_CLIMBING : 0;
    return flags;
}

static void ApplyFire(CPed* pPed, bool bOnFire)
{
    if (bOnFire && !pPed->m_pFire)
    {
        // a remote player's ped is fire proof (SET_CHAR_PROOFS): the flames only, no spreading (0 generations);
        // its damage is the owner's
        bool bFireProof = pPed->m_nPhysicalFlags.bFireProof;
        pPed->m_nPhysicalFlags.bFireProof = false;
        plugin::CallMethod<0x53A050, CFireManager*, CEntity*, CEntity*, float, uint8_t, unsigned int, int8_t>(
            &gFireManager, pPed, nullptr, 0.8f, 1, CLONE_FIRE_TIME, 0);  // CFireManager::StartFire(CEntity*)
        pPed->m_nPhysicalFlags.bFireProof = bFireProof;
    }
    else if (!bOnFire && pPed->m_pFire)
    {
        plugin::CallMethod<0x5393F0, CFire*>(pPed->m_pFire);  // CFire::Extinguish
    }
}

static void ApplySwimming(CPed* pPed, bool bSwimming)
{
    // our clone stands at the surface while its owner swims (the water event came on the owner's game only): the
    // response the engine gives (CEventInWater), the swim task follows from it
    if (bSwimming && !HasTask(pPed, TASK_SIMPLE_SWIM) && !HasTask(pPed, TASK_COMPLEX_IN_WATER) &&
        (pPed->m_nPhysicalFlags.bTouchingWater || pPed->m_nPhysicalFlags.bSubmergedInWater))
    {
        CTask* pTask = static_cast<CTask*>(CTask::operator new(IN_WATER_SIZE));
        plugin::CallMethod<0x6350D0, CTask*>(pTask);  // CTaskComplexInWater ctor
        pPed->m_pIntelligence->m_TaskMgr.SetTask(pTask, TASK_PRIMARY_EVENT_RESPONSE_NONTEMP, false);
    }
}

static void ApplyChoking(CPed* pPed, bool bChoking, bool bStarted)
{
    auto& taskManager = pPed->m_pIntelligence->m_TaskMgr;
    CTask* pPhysical = taskManager.m_aPrimaryTasks[TASK_PRIMARY_PHYSICAL_RESPONSE];
    bool bOurChoking = pPhysical && pPhysical->GetTaskType() == TASK_SIMPLE_CHOKING;

    if (bStarted && !HasTask(pPed, TASK_SIMPLE_CHOKING))
    {
        CTask* pTask = static_cast<CTask*>(CTask::operator new(CHOKING_SIZE));
        plugin::CallMethod<0x6202C0, CTask*, CPed*, bool>(pTask, nullptr, true);  // CTaskSimpleChoking ctor, tear gas
        taskManager.SetTask(pTask, TASK_PRIMARY_PHYSICAL_RESPONSE, false);
    }
    else if (!bChoking && bOurChoking)
    {
        taskManager.SetTask(nullptr, TASK_PRIMARY_PHYSICAL_RESPONSE, false);
    }
}

static void ApplyClimbing(CPed* pPed, bool bStarted)
{
    if (!bStarted || HasTask(pPed, TASK_SIMPLE_CLIMB))
    {
        return;
    }

    // the handhold near our clone (the owner's ped is a few cm off, the position sync keeps them together)
    CVector climbPos{};
    float fHeading = 0.0f;
    uint8_t surface = 0;
    CEntity* pEntity = plugin::CallAndReturn<CEntity*, 0x6803A0, CPed*, CVector&, float&, uint8_t&, bool>(
        pPed, climbPos, fHeading, surface, true);  // CTaskSimpleClimb::TestForClimb
    if (!pEntity)
    {
        return;
    }

    CTask* pTask = static_cast<CTask*>(CTask::operator new(CLIMB_SIZE));
    plugin::CallMethod<0x67A110, CTask*, CEntity*, CVector*, float, uint8_t, int8_t, bool>(pTask, pEntity, &climbPos,
        fHeading, surface, CLIMB_HEIGHT_GRAB, false);  // CTaskSimpleClimb ctor
    pPed->m_pIntelligence->m_TaskMgr.SetTask(pTask, TASK_PRIMARY_PRIMARY, false);
}

void CPedStateSync::Apply(CPed* pPed, uint8_t flags, uint8_t& lastFlags)
{
    uint8_t started = flags & ~lastFlags;
    lastFlags = flags;
    if (!pPed || !pPed->IsAlive() || pPed->m_nPedFlags.bInVehicle)
    {
        return;
    }

    ApplyFire(pPed, (flags & STATE_ON_FIRE) != 0);
    ApplySwimming(pPed, (flags & STATE_SWIMMING) != 0);
    ApplyChoking(pPed, (flags & STATE_CHOKING) != 0, (started & STATE_CHOKING) != 0);
    ApplyClimbing(pPed, (started & STATE_CLIMBING) != 0);
}
