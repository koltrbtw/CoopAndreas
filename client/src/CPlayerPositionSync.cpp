#include "stdafx.h"
#include "CPlayerPositionSync.h"

using Packets::Players::OnFootUpdate;

// further in one frame is a jump, not a run: a new epoch
static constexpr float JUMP_DISTANCE = 15.0f;
// the error a pull never catches up with: put it there (MTA: (5 + 5 * |v|) for 100 ms, half for our 50 ms)
static constexpr float SNAP_DISTANCE = 3.0f;
static constexpr float SNAP_DISTANCE_PER_SPEED = 5.0f;  // m per unit of m_vecMoveSpeed (1/50 s)
static constexpr unsigned int MAX_EXTRAPOLATION = 250;  // ms, a lost update must not send it flying

static float Dot(const CVector& a, const CVector& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static CVehicle* GetContactVehicle(vehicleid_t vehicleid)
{
    CNetworkVehicle* pNetworkVehicle = vehicleid.valid() ? CNetworkVehicleManager::GetVehicle(vehicleid) : nullptr;
    return pNetworkVehicle ? pNetworkVehicle->m_pVehicle : nullptr;
}

void CPlayerPositionSync::Collect(CPlayerPed* pPlayerPed, OnFootUpdate& packet)
{
    packet.epoch = ms_nEpoch;

    // what it stands on (CPed::m_pContactEntity, then m_standingOnEntity +0x568)
    CEntity* pContact = pPlayerPed->m_pContactEntity;
    if (!pContact || pContact->m_nType != ENTITY_TYPE_VEHICLE)
    {
        pContact = reinterpret_cast<CEntity*>(pPlayerPed->field_568);
    }
    if (!pContact || pContact->m_nType != ENTITY_TYPE_VEHICLE)
    {
        return;
    }

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(static_cast<CVehicle*>(pContact));
    if (!pNetworkVehicle)
    {
        return;
    }

    // in the vehicle's frame: right, forward and up are orthonormal, the transposed rotation turns it back
    const CMatrix& matrix = *pContact->m_matrix;
    CVector delta = pPlayerPed->GetPosition() - matrix.pos;
    CVector offset(Dot(delta, matrix.right), Dot(delta, matrix.up), Dot(delta, matrix.at));
    float fMax = OnFootUpdate::MAX_CONTACT_OFFSET;
    if (std::fabs(offset.x) < fMax && std::fabs(offset.y) < fMax && std::fabs(offset.z) < fMax)
    {
        packet.contactVehicle = pNetworkVehicle->m_vehicleId;
        packet.contactOffset = offset;
    }
}

bool CPlayerPositionSync::IsCurrent(CNetworkPlayer* pNetworkPlayer, const OnFootUpdate& packet)
{
    if (pNetworkPlayer->m_nEpoch < 0)
    {
        pNetworkPlayer->m_nEpoch = packet.epoch;
        return true;
    }

    // the epoch wraps: newer is up to half the range ahead
    int8_t nDiff = static_cast<int8_t>(packet.epoch - static_cast<uint8_t>(pNetworkPlayer->m_nEpoch));
    if (nDiff < 0)
    {
        return false;
    }
    if (nDiff > 0)
    {
        pNetworkPlayer->m_nEpoch = packet.epoch;
        pNetworkPlayer->m_bHasTargetPos = false;  // a jump: put it there
    }
    return true;
}

void CPlayerPositionSync::SetTarget(CNetworkPlayer* pNetworkPlayer, const OnFootUpdate& packet)
{
    CPlayerPed* pPlayerPed = pNetworkPlayer->m_pPed;
    pNetworkPlayer->m_vecTargetPos = packet.vecPos;
    pNetworkPlayer->m_vecTargetVelocity = packet.vecMoveSpeed;
    pNetworkPlayer->m_nTargetPosTime = CTimer::m_snTimeInMilliseconds;
    pNetworkPlayer->m_targetContactVehicle = GetContactVehicle(packet.contactVehicle) ? packet.contactVehicle : vehicleid_t();
    pNetworkPlayer->m_vecTargetContactOffset = packet.contactOffset;

    if (ms_bOldWay)
    {
        pPlayerPed->SetPosn(packet.vecPos);
        return;
    }

    float fSnap = SNAP_DISTANCE + SNAP_DISTANCE_PER_SPEED * CVector(packet.vecMoveSpeed).Magnitude();
    if (!pNetworkPlayer->m_bHasTargetPos || DistanceBetweenPoints(pPlayerPed->GetPosition(), packet.vecPos) > fSnap)
    {
        ms_nSnaps++;
        pPlayerPed->SetPosn(packet.vecPos);
    }
    pNetworkPlayer->m_bHasTargetPos = true;
}

void CPlayerPositionSync::Warp(CNetworkPlayer* pNetworkPlayer, const CVector& pos)
{
    pNetworkPlayer->m_vecTargetPos = pos;
    pNetworkPlayer->m_vecTargetVelocity = CVector(0.0f, 0.0f, 0.0f);
    pNetworkPlayer->m_nTargetPosTime = CTimer::m_snTimeInMilliseconds;
    pNetworkPlayer->m_targetContactVehicle = vehicleid_t();
    pNetworkPlayer->m_bHasTargetPos = false;
}

void CPlayerPositionSync::ProcessClone(CNetworkPlayer* pNetworkPlayer)
{
    CPlayerPed* pPlayerPed = pNetworkPlayer->m_pPed;
    // a climb task holds it at the handhold (CPedStateSync)
    if (ms_bOldWay || !pPlayerPed || !pNetworkPlayer->m_bHasTargetPos || pPlayerPed->m_nPedFlags.bInVehicle ||
        pPlayerPed->m_pAttachedTo || CPedStateSync::IsClimbing(pPlayerPed))
    {
        return;
    }

    // the enter task walks it to the door (VEHICLE_ENTER), the position of the sender would fight it
    CNetworkVehicle* pEnterVehicle = CNetworkVehicleManager::GetVehicle(pNetworkPlayer->m_enterVehicleId);
    if (pEnterVehicle && pNetworkPlayer->IsEnteringVehicle(pEnterVehicle->m_pVehicle))
    {
        return;
    }

    // where the sender is now: on its vehicle (our copy of it moved on since), or the last position moved on by its
    // speed (m_vecMoveSpeed is per 1/50 s)
    CVector predicted;
    if (CVehicle* pVehicle = GetContactVehicle(pNetworkPlayer->m_targetContactVehicle))
    {
        predicted = *pVehicle->m_matrix * pNetworkPlayer->m_vecTargetContactOffset;
    }
    else
    {
        unsigned int nElapsed =
            std::min(CTimer::m_snTimeInMilliseconds - pNetworkPlayer->m_nTargetPosTime, MAX_EXTRAPOLATION);
        predicted = pNetworkPlayer->m_vecTargetPos + pNetworkPlayer->m_vecTargetVelocity * (nElapsed / 20.0f);
    }

    CVector error = predicted - pPlayerPed->GetPosition();
    float fSnap = SNAP_DISTANCE + SNAP_DISTANCE_PER_SPEED * pNetworkPlayer->m_vecTargetVelocity.Magnitude();
    if (error.Magnitude() > fSnap)
    {
        ms_nSnaps++;
        pPlayerPed->SetPosn(predicted);
        return;
    }

    float fPull = std::min(1.0f, 0.1f * CTimer::ms_fTimeStep);
    pPlayerPed->SetPosn(pPlayerPed->GetPosition() + error * fPull);
}

void CPlayerPositionSync::Process()
{
    // ours: a jump since the last frame starts a new epoch
    if (CPlayerPed* pPlayerPed = FindPlayerPed(0))
    {
        CVector pos = pPlayerPed->GetPosition();
        if (ms_bHasLastPos && DistanceBetweenPoints(pos, ms_vecLastPos) > JUMP_DISTANCE)
        {
            ms_nEpoch++;
            CTask* pTask = pPlayerPed->m_pIntelligence->m_TaskMgr.GetActiveTask();
            logger::info("our position jumped %.0f m: epoch %d, in vehicle %d, task %d", DistanceBetweenPoints(pos, ms_vecLastPos),
                ms_nEpoch, pPlayerPed->m_nPedFlags.bInVehicle, pTask ? pTask->GetTaskType() : -1);
        }
        ms_vecLastPos = pos;
        ms_bHasLastPos = true;
    }

    for (CNetworkPlayer* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        ProcessClone(pNetworkPlayer);
    }
}

void CPlayerPositionSync::Reset()
{
    ms_bHasLastPos = false;
}
