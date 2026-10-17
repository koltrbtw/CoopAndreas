#pragma once

// the local player knocked down on foot (hit by a vehicle, an explosion, a punch): CTaskComplexFallAndGetUp of the
// owner goes as PLAYER_FALL, the clone plays it. The clone takes no damage events of its own (DamageHooks), its pad
// alone would keep it standing
class CPlayerFallSync
{
public:
    static void Process();
    static void Reset();
    // the fall went with VEHICLE_EXIT already
    static void OnFallSent(CTask* pFall);
    static void HandlePacket(const Packets::Players::PlayerFall& packet);

private:
    static inline CTask* ms_pLastFall = nullptr;  // the fall task of the last frame, a new one is sent
};
