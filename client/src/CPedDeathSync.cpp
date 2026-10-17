#include "stdafx.h"
#include "CPedDeathSync.h"
#include "CPedAnimSync.h"

using Packets::Peds::PedDied;

// CTaskComplexDie (gta-reversed TaskComplexDie.h, 0x28 bytes): weapon, anim group, anim, blend delta, anim speed,
// flags (stealth, falling to death, over a railing), the fall direction
static constexpr unsigned int COMPLEX_DIE_SIZE = 0x28;
static constexpr unsigned int COMPLEX_DIE_WEAPON = 0xC;
static constexpr unsigned int COMPLEX_DIE_ANIM_GROUP = 0x10;
static constexpr unsigned int COMPLEX_DIE_ANIM_ID = 0x14;
static constexpr unsigned int COMPLEX_DIE_BLEND_DELTA = 0x18;
static constexpr unsigned int COMPLEX_DIE_ANIM_SPEED = 0x1C;
static constexpr unsigned int COMPLEX_DIE_FLAGS = 0x20;
static constexpr unsigned int COMPLEX_DIE_FALL_DIRECTION = 0x24;
// CTaskSimpleDead, 0x10 bytes
static constexpr unsigned int SIMPLE_DEAD_SIZE = 0x10;
// CTaskSimpleStealthKill, 0x20 bytes: keep the target alive (the killer's part) +8, the target +0xC, the group +0x10
static constexpr unsigned int STEALTH_KILL_SIZE = 0x20;
static constexpr unsigned int STEALTH_KILL_KEEP_ALIVE = 0x8;
static constexpr unsigned int STEALTH_KILL_TARGET = 0xC;
static constexpr unsigned int STEALTH_KILL_GROUP = 0x10;

static constexpr int ANIM_KILL_KNIFE_PED_DIE = 350;  // ANIM_STEALTH_KN_KILL_KNIFE_PED_DIE
// an owner without health for that long sent no death we got (it came in our range dead): the default death here
static constexpr unsigned int MISSED_DEATH_TIME = 1500;

template <typename T>
static T& TaskField(CTask* pTask, unsigned int nOffset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(pTask) + nOffset);
}

static bool IsDying(CPed* pPed)
{
    return !pPed->IsAlive() || pPed->m_ePedState == PEDSTATE_DIE_BY_STEALTH;
}

static CTask* FindPrimaryTask(CPed* pPed, int nTaskType)
{
    for (CTask* pTask : pPed->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks)
    {
        if (pTask && pTask->GetTaskType() == nTaskType)
        {
            return pTask;
        }
    }
    return nullptr;
}

bool CPedDeathSync::IsClone(CPed* pPed)
{
    if (CNetworkPlayerManager::GetPlayer(pPed))
    {
        return true;
    }
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPed);
    return pNetworkPed && !pNetworkPed->m_bSyncing;
}

void CPedDeathSync::CollectDeath(CPed* pPed, PedDied& packet)
{
    packet.entity.SetEntity(pPed);
    packet.weapon = static_cast<uint8_t>(pPed->m_nLastWeaponDamage);

    if (CTask* pStealth = FindPrimaryTask(pPed, TASK_SIMPLE_STEALTH_KILL))
    {
        packet.flags |= PedDied::FLAG_STEALTH;
        packet.weapon = WEAPON_KNIFE;
        packet.animGroup = static_cast<uint8_t>(TaskField<int>(pStealth, STEALTH_KILL_GROUP));
        packet.animId = ANIM_KILL_KNIFE_PED_DIE;
        packet.blendDelta = 8.0f;
        packet.animSpeed = 1.0f;
        return;
    }

    if (CTask* pDie = FindPrimaryTask(pPed, TASK_COMPLEX_DIE))
    {
        packet.weapon = static_cast<uint8_t>(TaskField<int>(pDie, COMPLEX_DIE_WEAPON));
        packet.animGroup = static_cast<uint8_t>(TaskField<int>(pDie, COMPLEX_DIE_ANIM_GROUP));
        packet.animId = static_cast<uint16_t>(TaskField<int>(pDie, COMPLEX_DIE_ANIM_ID));
        packet.blendDelta = TaskField<float>(pDie, COMPLEX_DIE_BLEND_DELTA);
        packet.animSpeed = TaskField<float>(pDie, COMPLEX_DIE_ANIM_SPEED);
        uint32_t nFlags = TaskField<uint32_t>(pDie, COMPLEX_DIE_FLAGS);
        packet.flags |= (nFlags & 2) ? PedDied::FLAG_FALLING_TO_DEATH : 0;
        packet.flags |= (nFlags & 4) ? PedDied::FLAG_OVER_RAILING : 0;
        packet.fallDirection = static_cast<uint8_t>(TaskField<int>(pDie, COMPLEX_DIE_FALL_DIRECTION));
        return;
    }

    // no die task: dead already (CTaskSimpleDead), drowned or killed by a script; the clone falls with the default
    packet.animGroup = ANIM_GROUP_DEFAULT;
    packet.animId = ANIM_DEFAULT_KO_SHOT_FRONT_0;
    if (pPed->m_nPedFlags.bIsDrowning)
    {
        packet.flags |= PedDied::FLAG_DROWNED;
    }
}

// the clone dies as its owner's ped did
static void KillClone(CPed* pPed, const PedDied& packet)
{
    pPed->m_fHealth = 0.0f;
    pPed->m_fArmour = 0.0f;
    if (CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPed))
    {
        pNetworkPed->m_fHealth = 0.0f;
    }

    auto& taskManager = pPed->m_pIntelligence->m_TaskMgr;
    bool bStealth = (packet.flags & PedDied::FLAG_STEALTH) != 0;

    // our copy of the stealth kill plays the victim's anim and ends on the ground: lie dead after it
    if (bStealth && FindPrimaryTask(pPed, TASK_SIMPLE_STEALTH_KILL))
    {
        CTask* pDead = static_cast<CTask*>(CTask::operator new(SIMPLE_DEAD_SIZE));
        plugin::CallMethod<0x630590, CTask*, unsigned int, bool>(pDead, CTimer::m_snTimeInMilliseconds, false);  // CTaskSimpleDead ctor
        taskManager.SetTask(pDead, TASK_PRIMARY_EVENT_RESPONSE_NONTEMP, false);
        return;
    }

    int nGroup = packet.animGroup;
    int nAnimId = packet.animId;
    if (!CPedAnimSync::IsGroupLoaded(nGroup))
    {
        nGroup = ANIM_GROUP_DEFAULT;
        nAnimId = ANIM_DEFAULT_KO_SHOT_FRONT_0;
    }

    // what the owner's damage response does: the physical response (a fall, a hit) goes, the die task comes
    taskManager.SetTask(nullptr, TASK_PRIMARY_PHYSICAL_RESPONSE, false);
    taskManager.SetTask(nullptr, TASK_PRIMARY_EVENT_RESPONSE_TEMP, false);

    CTask* pDie = static_cast<CTask*>(CTask::operator new(COMPLEX_DIE_SIZE));
    plugin::CallMethod<0x630040, CTask*, int, int, int, float, float, bool, bool, int, bool>(pDie, packet.weapon, nGroup,
        nAnimId, packet.blendDelta, packet.animSpeed, bStealth, (packet.flags & PedDied::FLAG_FALLING_TO_DEATH) != 0,
        packet.fallDirection, (packet.flags & PedDied::FLAG_OVER_RAILING) != 0);  // CTaskComplexDie ctor
    taskManager.SetTask(pDie, TASK_PRIMARY_EVENT_RESPONSE_NONTEMP, false);
}

void CPedDeathSync::HandleDied(const PedDied& packet)
{
    PedDied copy = packet;
    CPed* pPed = static_cast<CPed*>(copy.entity.GetEntity());
    // dying by our copy of a stealth kill still takes it: it lies dead after the anim
    if (!pPed || pPed == FindPlayerPed(0) || !IsClone(pPed) || !pPed->IsAlive())
    {
        return;
    }

    logger::info("death of %s: weapon %d anim %d/%d flags %d", pPed->IsPlayer() ? "a player" : "a ped", packet.weapon,
        packet.animGroup, packet.animId, packet.flags);
    KillClone(pPed, packet);
}

static void StartStealthKill(CPed* pPed, bool bKiller, CPed* pTarget, int nGroup)
{
    // the replayed pad of the killer or the engine started it already
    if (FindPrimaryTask(pPed, TASK_SIMPLE_STEALTH_KILL))
    {
        return;
    }

    CTask* pTask = static_cast<CTask*>(CTask::operator new(STEALTH_KILL_SIZE));
    plugin::CallMethod<0x6225F0, CTask*, bool, CPed*, int>(pTask, bKiller, pTarget, nGroup);  // CTaskSimpleStealthKill ctor
    if (bKiller)
    {
        pPed->m_pIntelligence->m_TaskMgr.SetTask(pTask, TASK_PRIMARY_PRIMARY, false);
        return;
    }

    // as CEventHandler::ComputeDamageResponse (0x4C0338) does for the victim
    pPed->m_pIntelligence->m_TaskMgr.SetTask(pTask, TASK_PRIMARY_PHYSICAL_RESPONSE, false);
    pPed->SetPedState(PEDSTATE_DIE_BY_STEALTH);
}

void CPedDeathSync::HandleStealthKill(const Packets::Peds::PedStealthKill& packet)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(packet.senderPlayerid);
    Packets::Peds::PedStealthKill copy = packet;
    CPed* pVictim = static_cast<CPed*>(copy.victim.GetEntity());
    if (!pNetworkPlayer || !pNetworkPlayer->m_pPed || !pVictim || IsDying(pVictim))
    {
        return;
    }

    logger::info("%s stabs %s", pNetworkPlayer->GetName().c_str(), pVictim == FindPlayerPed(0) ? "us" : "a ped");
    StartStealthKill(pNetworkPlayer->m_pPed, true, pVictim, packet.animGroup);
    // ours: the engine kills it at the end of the anim (its own damage event), PED_DIED goes from here
    StartStealthKill(pVictim, false, pNetworkPlayer->m_pPed, packet.animGroup);
}

void CPedDeathSync::Process()
{
    if (CPlayerPed* pPlayerPed = FindPlayerPed(0))
    {
        bool bDying = IsDying(pPlayerPed);
        if (bDying && !ms_bLocalDeathSent)
        {
            PedDied packet{};
            CollectDeath(pPlayerPed, packet);
            GetPacketFactory().Send(packet);
            logger::info("we died: weapon %d anim %d/%d flags %d", packet.weapon, packet.animGroup, packet.animId,
                packet.flags);
        }
        ms_bLocalDeathSent = bDying;

        // a stealth kill we started: the victim's owner plays its part, the others both parts
        CTask* pStealth = pPlayerPed->m_pIntelligence->m_TaskMgr.FindActiveTaskByType(TASK_SIMPLE_STEALTH_KILL);
        if (pStealth && pStealth != ms_pLastStealthKill && TaskField<bool>(pStealth, STEALTH_KILL_KEEP_ALIVE))
        {
            CPed* pVictim = TaskField<CPed*>(pStealth, STEALTH_KILL_TARGET);
            Packets::Peds::PedStealthKill packet{};
            packet.victim.SetEntity(pVictim);
            if (pVictim && (packet.victim.pedid.valid() || packet.victim.playerid.valid()))
            {
                packet.animGroup = static_cast<uint8_t>(TaskField<int>(pStealth, STEALTH_KILL_GROUP));
                GetPacketFactory().Send(packet);
                logger::info("we stab %s", IsClone(pVictim) ? "a clone" : "our ped");
                // our own damage event can't reach a clone: its part plays here as on the others' games
                if (IsClone(pVictim))
                {
                    StartStealthKill(pVictim, false, pPlayerPed, packet.animGroup);
                }
            }
        }
        ms_pLastStealthKill = pStealth;
    }

    for (CNetworkPed* pNetworkPed : CNetworkPedManager::m_pPeds)
    {
        CPed* pPed = pNetworkPed->m_pPed;
        if (!pPed)
        {
            continue;
        }

        bool bDying = IsDying(pPed);
        if (pNetworkPed->m_bSyncing)
        {
            if (bDying && !pNetworkPed->m_bDeathSent)
            {
                PedDied packet{};
                CollectDeath(pPed, packet);
                GetPacketFactory().Send(packet);
            }
            pNetworkPed->m_bDeathSent = bDying;
            continue;
        }

        // a clone whose death we missed (it came in our range dead, the packet came before its spawn)
        if (pNetworkPed->m_fHealth >= 1.0f || bDying)
        {
            pNetworkPed->m_nNoHealthSince = 0;
        }
        else if (pNetworkPed->m_nNoHealthSince == 0)
        {
            pNetworkPed->m_nNoHealthSince = CTimer::m_snTimeInMilliseconds;
        }
        else if (CTimer::m_snTimeInMilliseconds - pNetworkPed->m_nNoHealthSince > MISSED_DEATH_TIME)
        {
            PedDied packet{};
            packet.animGroup = ANIM_GROUP_DEFAULT;
            packet.animId = ANIM_DEFAULT_KO_SHOT_FRONT_0;
            logger::info("ped %s has had no health for %u ms, dies here", to_string(pNetworkPed->m_pedId).c_str(),
                MISSED_DEATH_TIME);
            KillClone(pPed, packet);
            pNetworkPed->m_nNoHealthSince = 0;
        }
    }
}

void CPedDeathSync::Reset()
{
    ms_bLocalDeathSent = false;
    ms_pLastStealthKill = nullptr;
}
