#include "stdafx.h"
#include "CPopulationHandover.h"
#include "Hooks/WorldHooks.h"
#include <CVisibilityPlugins.h>

CNetworkPlayer* CPopulationHandover::FindNearestRemotePlayer(const CVector& position, float fMaxDistance)
{
    CNetworkPlayer* pNearest = nullptr;
    float fNearestDistance = fMaxDistance;

    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (!pNetworkPlayer->m_pPed)
        {
            continue;
        }

        float fDistance = (pNetworkPlayer->m_pPed->GetPosition() - position).Magnitude();
        if (fDistance < fNearestDistance)
        {
            pNearest = pNetworkPlayer;
            fNearestDistance = fDistance;
        }
    }

    return pNearest;
}

// our ambient on foot ped that could go to someone else
static CNetworkPed* GetOwnedAmbientPed(CPed* pPed)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPed);
    if (!pNetworkPed || !pNetworkPed->m_bSyncing || pNetworkPed->m_nCreatedBy != RANDOM_CHAR ||
        pPed->m_nPedFlags.bInVehicle || pPed->m_fHealth <= 0.0f)
    {
        return nullptr;
    }
    return pNetworkPed;
}

// our ambient vehicle that could go to someone else (not the one we sit in)
static CNetworkVehicle* GetOwnedAmbientVehicle(CVehicle* pVehicle)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle);
    if (!pNetworkVehicle || !pNetworkVehicle->m_bSyncing || pNetworkVehicle->m_nCreatedBy == MISSION_VEHICLE ||
        pVehicle->m_nStatus == STATUS_WRECKED)
    {
        return nullptr;
    }

    CPlayerPed* pLocalPlayer = FindPlayerPed(0);
    if (pLocalPlayer && pLocalPlayer->m_pVehicle == pVehicle)
    {
        return nullptr;
    }

    // a towed vehicle goes together with its tractor, see HandOverVehicle
    if (pVehicle->m_pTractor)
    {
        return nullptr;
    }
    return pNetworkVehicle;
}

void CPopulationHandover::HandOverPed(CNetworkPed* pNetworkPed, CNetworkPlayer* pTarget)
{
    CPed* pPed = pNetworkPed->m_pPed;

    Packets::Peds::PedHandoff handoff{};
    handoff.pedid = pNetworkPed->m_pedId;
    handoff.released = true;
    handoff.target = pTarget->m_playerId;
    handoff.pos = pPed->GetPosition();
    handoff.heading = pPed->m_fCurrentRotation;

    pNetworkPed->BecomeClone(true);
    GetPacketFactory().Send(handoff);

    // the removal may come at the end of a fade out, the ped must stay visible
    pPed->m_nPedFlags.bFadeOut = false;
    if (pPed->m_pRwClump)
    {
        CVisibilityPlugins::SetClumpAlpha(pPed->m_pRwClump, 255);
    }
}

void CPopulationHandover::HandOverVehicle(CNetworkVehicle* pNetworkVehicle, CNetworkPlayer* pTarget)
{
    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;

    CVehicle* pTrailer = pVehicle->m_pTrailer;

    Packets::Vehicles::VehicleHandoff handoff{};
    pNetworkVehicle->BecomeClone(&handoff);
    handoff.target = pTarget->m_playerId;
    GetPacketFactory().Send(handoff);

    pVehicle->m_nVehicleFlags.bFadeOut = false;
    if (pVehicle->m_pRwClump)
    {
        CVisibilityPlugins::SetClumpAlpha(pVehicle->m_pRwClump, 255);
    }

    // what it tows goes with it, the new syncer drives both
    CNetworkVehicle* pNetworkTrailer = pTrailer ? CNetworkVehicleManager::GetVehicle(pTrailer) : nullptr;
    if (pNetworkTrailer && pNetworkTrailer->m_bSyncing)
    {
        HandOverVehicle(pNetworkTrailer, pTarget);
    }
}

// the player to give an entity our population code is about to remove: one that needs it more than we do
static CNetworkPlayer* FindRemovalTarget(const CVector& position, float fMaxDistance, unsigned int nOwnershipChangeTime)
{
    constexpr float CLOSER_FACTOR_SQ = 0.8f;

    CPlayerPed* pLocalPlayer = FindPlayerPed(0);
    if (!pLocalPlayer ||
        CTimer::m_snTimeInMilliseconds - nOwnershipChangeTime < CPopulationHandover::REMOVAL_HANDOVER_HOLD_TIME)
    {
        return nullptr;
    }

    CNetworkPlayer* pTarget = CPopulationHandover::FindNearestRemotePlayer(position, fMaxDistance);
    if (!pTarget)
    {
        return nullptr;
    }

    float fOwnerDistance = (pLocalPlayer->GetPosition() - position).Magnitude();
    float fTargetDistance = (pTarget->m_pPed->GetPosition() - position).Magnitude();
    return fTargetDistance * fTargetDistance < fOwnerDistance * fOwnerDistance * CLOSER_FACTOR_SQ ? pTarget : nullptr;
}

bool CPopulationHandover::TryHandOverPed(CPed* pPed)
{
    if (!CNetwork::m_bAuthenticated)
    {
        return false;
    }

    CNetworkPed* pNetworkPed = GetOwnedAmbientPed(pPed);
    CNetworkPlayer* pTarget =
        pNetworkPed ? FindRemovalTarget(pPed->GetPosition(), PED_HANDOVER_DISTANCE, pNetworkPed->m_nOwnershipChangeTime)
                    : nullptr;
    if (!pTarget)
    {
        return false;
    }

    HandOverPed(pNetworkPed, pTarget);
    logger::info("ped %s handed over to %s instead of removing", to_string(pNetworkPed->m_pedId).c_str(),
        to_string(pTarget->m_playerId).c_str());
    return true;
}

bool CPopulationHandover::TryHandOverVehicle(CVehicle* pVehicle)
{
    if (!CNetwork::m_bAuthenticated)
    {
        return false;
    }

    CNetworkVehicle* pNetworkVehicle = GetOwnedAmbientVehicle(pVehicle);
    CNetworkPlayer* pTarget = pNetworkVehicle ? FindRemovalTarget(pVehicle->GetPosition(), VEHICLE_HANDOVER_DISTANCE,
                                                    pNetworkVehicle->m_nOwnershipChangeTime)
                                              : nullptr;
    if (!pTarget)
    {
        return false;
    }

    HandOverVehicle(pNetworkVehicle, pTarget);
    logger::info("vehicle %s handed over to %s instead of removing", to_string(pNetworkVehicle->m_vehicleId).c_str(),
        to_string(pTarget->m_playerId).c_str());
    return true;
}

// a remote player to give the entity to, if it is clearly closer to it than we are (rules of
// NetObjProximityMigrateable::TryToPassControlProximity in GTA V, see .claude/features/zone-streaming/DESIGN.md)
static CNetworkPlayer* FindMigrationTarget(const CVector& position, unsigned int nOwnershipChangeTime)
{
    constexpr unsigned int HOLD_TIME = 3000;  // no ping-pong right after a change
    constexpr float MAX_TARGET_DISTANCE = 50.0f;
    constexpr float MIN_OWNER_DISTANCE = 4.0f;
    constexpr float CLOSER_FACTOR_SQ = 0.8f;

    CPlayerPed* pLocalPlayer = FindPlayerPed(0);
    if (!pLocalPlayer || CTimer::m_snTimeInMilliseconds - nOwnershipChangeTime < HOLD_TIME)
    {
        return nullptr;
    }

    float fOwnerDistance = (pLocalPlayer->GetPosition() - position).Magnitude();
    if (fOwnerDistance < MIN_OWNER_DISTANCE)
    {
        return nullptr;
    }

    CNetworkPlayer* pTarget = CPopulationHandover::FindNearestRemotePlayer(position, MAX_TARGET_DISTANCE);
    if (!pTarget)
    {
        return nullptr;
    }

    float fTargetDistance = (pTarget->m_pPed->GetPosition() - position).Magnitude();
    return fTargetDistance * fTargetDistance < fOwnerDistance * fOwnerDistance * CLOSER_FACTOR_SQ ? pTarget : nullptr;
}

void CPopulationHandover::Process()
{
    static unsigned int nLastCheck = 0;
    if (CTimer::m_snTimeInMilliseconds - nLastCheck < 500 || CNetworkPlayerManager::m_pPlayers.empty())
    {
        return;
    }
    nLastCheck = CTimer::m_snTimeInMilliseconds;

    // the copies: a handoff turns our entity into a clone, the lists don't change
    for (auto pNetworkVehicle : std::vector<CNetworkVehicle*>(CNetworkVehicleManager::m_pVehicles))
    {
        if (!pNetworkVehicle->m_pVehicle || !GetOwnedAmbientVehicle(pNetworkVehicle->m_pVehicle))
        {
            continue;
        }

        if (auto pTarget = FindMigrationTarget(pNetworkVehicle->m_pVehicle->GetPosition(), pNetworkVehicle->m_nOwnershipChangeTime))
        {
            HandOverVehicle(pNetworkVehicle, pTarget);
            logger::info("vehicle %s migrated to the nearer %s", to_string(pNetworkVehicle->m_vehicleId).c_str(),
                to_string(pTarget->m_playerId).c_str());
        }
    }

    for (auto pNetworkPed : std::vector<CNetworkPed*>(CNetworkPedManager::m_pPeds))
    {
        if (!pNetworkPed->m_pPed || !GetOwnedAmbientPed(pNetworkPed->m_pPed))
        {
            continue;
        }

        if (auto pTarget = FindMigrationTarget(pNetworkPed->m_pPed->GetPosition(), pNetworkPed->m_nOwnershipChangeTime))
        {
            HandOverPed(pNetworkPed, pTarget);
            logger::info("ped %s migrated to the nearer %s", to_string(pNetworkPed->m_pedId).c_str(),
                to_string(pTarget->m_playerId).c_str());
        }
    }
}

// the only removal in CPopulation::ManagePed: far away, the timer ran out, or the end of a fade out
static void __cdecl CPopulation__ManagePed_RemovePed_Hook(CPed* pPed)
{
    if (CPopulationHandover::TryHandOverPed(pPed))
    {
        return;
    }

    CPopulation::RemovePed(pPed);
}

static void __cdecl RemoveVehicleOrHandOver(CVehicle* pVehicle)
{
    if (CPopulationHandover::TryHandOverVehicle(pVehicle))
    {
        return;
    }

    WorldHooks::CWorld__Remove_Hook(pVehicle);
    delete pVehicle;  // the scalar deleting destructor, as the original `push 1; call [vtbl]`
}

// CCarCtrl::PossiblyRemoveVehicle removes inline, `push esi; call CWorld::Remove; ...; call [vtbl]` (17 bytes),
// with esi = vehicle; each stub replaces one of them and returns right after it
static void __declspec(naked) CCarCtrl__PossiblyRemoveVehicle_Remove1()
{
    __asm
    {
        push esi
        call RemoveVehicleOrHandOver
        add esp, 4
        push 0x4251F6
        ret
    }
}

static void __declspec(naked) CCarCtrl__PossiblyRemoveVehicle_Remove2()
{
    __asm
    {
        push esi
        call RemoveVehicleOrHandOver
        add esp, 4
        push 0x425231
        ret
    }
}

static void __declspec(naked) CCarCtrl__PossiblyRemoveVehicle_Remove3()
{
    __asm
    {
        push esi
        call RemoveVehicleOrHandOver
        add esp, 4
        push 0x42542E
        ret
    }
}

// entering or leaving an interior removes every random ped in the world, also the ones other players see
static void __cdecl CPopulation__RemoveAllRandomPeds_Hook()
{
    if (CNetwork::m_bAuthenticated)
    {
        // TryHandOverPed doesn't remove from the list, it turns our ped into a clone
        for (auto pNetworkPed : std::vector<CNetworkPed*>(CNetworkPedManager::m_pPeds))
        {
            if (pNetworkPed->m_pPed)
            {
                CPopulationHandover::TryHandOverPed(pNetworkPed->m_pPed);
            }
        }
    }

    CPopulation::RemoveAllRandomPeds();
}

void CPopulationHandover::InjectHooks()
{
    patch::RedirectCall(0x612227, CPopulation__ManagePed_RemovePed_Hook);

    // must come after WorldHooks: these overlap its CWorld::Remove call sites 0x4251E6, 0x425221, 0x42541E
    patch::RedirectJump(0x4251E5, CCarCtrl__PossiblyRemoveVehicle_Remove1);  // faded out / overturned
    patch::RedirectJump(0x425220, CCarCtrl__PossiblyRemoveVehicle_Remove2);  // far and not on screen
    patch::RedirectJump(0x42541D, CCarCtrl__PossiblyRemoveVehicle_Remove3);  // stuck / wreck

    patch::RedirectCall(0x4407B7, CPopulation__RemoveAllRandomPeds_Hook);  // CEntryExit::TransitionFinished
    patch::RedirectCall(0x61648C, CPopulation__RemoveAllRandomPeds_Hook);  // CPopulation::PopulateInterior
}
