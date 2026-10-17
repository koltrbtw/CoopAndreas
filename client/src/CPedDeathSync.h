#pragma once

// deaths and stealth kills: a clone takes no damage of its own (DamageHooks), its owner says when it dies and
// with which anim (PED_DIED), the killer's game says who it stabs (PED_STEALTH_KILL). The pickups a body leaves
// (weapons, money) are made by the owner's game alone
class CPedDeathSync
{
public:
    static void Process();
    static void Reset();
    static void HandleDied(const Packets::Peds::PedDied& packet);
    static void HandleStealthKill(const Packets::Peds::PedStealthKill& packet);
    // a ped clone, a remote player's ped: no pickups of its body here
    static bool IsClone(CPed* pPed);

private:
    static void CollectDeath(CPed* pPed, Packets::Peds::PedDied& packet);

    static inline bool ms_bLocalDeathSent = false;
    static inline CTask* ms_pLastStealthKill = nullptr;  // the local player's killer task of the last frame
};
