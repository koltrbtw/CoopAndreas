#pragma once

// the position of a remote player's clone on foot. The clone walks on the sender's pad by itself, the updates only
// correct its drift (as CNetworkPed's target, MTA's CClientPed::SetTargetPosition): setting the packet position every
// 50 ms pulled it back to where the sender was a moment ago. Standing on a network vehicle the position goes in the
// vehicle's frame, an epoch drops the updates sent before a jump (a teleport, a respawn, an interior)
class CPlayerPositionSync
{
public:
    static inline unsigned int ms_nSnaps = 0;  // clones put at the target at once, for the stand
    static inline bool ms_bOldWay = false;     // the stand's baseline: the packet position set as it comes

    // every frame: our epoch, the drift of the clones
    static void Process();
    static void Reset();
    // ours: the epoch and the vehicle we stand on into an on foot update
    static void Collect(CPlayerPed* pPlayerPed, Packets::Players::OnFootUpdate& packet);
    // a clone: false when the update is older than the last jump (drop it)
    static bool IsCurrent(CNetworkPlayer* pNetworkPlayer, const Packets::Players::OnFootUpdate& packet);
    // a clone: the target from an on foot update
    static void SetTarget(CNetworkPlayer* pNetworkPlayer, const Packets::Players::OnFootUpdate& packet);
    // a clone: put there at once (a teleport of the sender, its interior), the next update corrects from there
    static void Warp(CNetworkPlayer* pNetworkPlayer, const CVector& pos);

private:
    static void ProcessClone(CNetworkPlayer* pNetworkPlayer);

    static inline uint8_t ms_nEpoch = 0;
    static inline CVector ms_vecLastPos{};
    static inline bool ms_bHasLastPos = false;
};
