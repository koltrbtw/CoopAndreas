#pragma once

// states a clone can't get by itself (DamageHooks: no damage events, a ped clone no events at all): on fire, swimming,
// choking (tear gas, a spray), climbing. The owner sends them as flags in the on foot updates, the clone starts or
// ends the same task (as MTA's SetOnFire, RunSwimTask, SetChoking, RunClimbingTask)
class CPedStateSync
{
public:
    static constexpr uint8_t STATE_ON_FIRE = 1;
    static constexpr uint8_t STATE_SWIMMING = 2;
    static constexpr uint8_t STATE_CHOKING = 4;
    static constexpr uint8_t STATE_CLIMBING = 8;

    static uint8_t Collect(CPed* pPed);
    // lastFlags: the flags of the last update of this clone; choking and climbing start on the change only (their
    // length is random, a clone done before its owner must not start again)
    static void Apply(CPed* pPed, uint8_t flags, uint8_t& lastFlags);
    // the position sync leaves a climbing clone to its climb task (it holds the ped at the handhold)
    static bool IsClimbing(CPed* pPed);
};
