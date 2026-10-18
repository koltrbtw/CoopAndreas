#include "stdafx.h"
#include <CPedGroups.h>
#include "CPopulationHandover.h"
#include "Hooks/WorldHooks.h"
#include <CVisibilityPlugins.h>

CNetworkPlayer* CPopulationHandover::FindNearestRemotePlayer(const CVector& position, float fMaxDistance, int nAreaCode)
{
    CNetworkPlayer* pNearest = nullptr;
    float fNearestDistance = fMaxDistance;

    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (!pNetworkPlayer->m_pPed || (nAreaCode >= 0 && pNetworkPlayer->m_pPed->m_nAreaCode != nAreaCode))
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
// a follower in a player's group (recruited homies): it goes where its leader goes, a handoff to someone else would
// leave it in a group of the old owner's game only
static bool IsInPlayerGroup(CPed* pPed)
{
    CPedGroup* pGroup = CPedGroups::GetPedsGroup(pPed);
    CPed* pLeader = pGroup ? pGroup->m_groupMembership.GetLeader() : nullptr;
    return pLeader && pLeader != pPed && pLeader->IsPlayer();
}

// dying (a stealth kill too) or dead: a new syncer would flush its die task (BecomeClone here) and give the body
// an ambient life (BecomeSyncer there)
static bool IsDyingOrDead(CPed* pPed)
{
    return pPed->m_fHealth <= 0.0f || !pPed->IsAlive() || pPed->m_ePedState == PEDSTATE_DIE_BY_STEALTH;
}

static CNetworkPed* GetOwnedAmbientPed(CPed* pPed)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPed);
    if (!pNetworkPed || !pNetworkPed->m_bSyncing || pNetworkPed->m_nCreatedBy != RANDOM_CHAR ||
        pPed->m_nPedFlags.bInVehicle || IsDyingOrDead(pPed) || IsInPlayerGroup(pPed))
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

    // a towed vehicle goes together with its tractor, see HandOverVehicle; a train only when the generator removes it
    if (pVehicle->m_pTractor || pNetworkVehicle->m_bTrain)
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

    // the tasks of a mission ped are the script's, the clone keeps them for when the ped comes back
    pNetworkPed->BecomeClone(pNetworkPed->m_nCreatedBy != MISSION_CHAR);
    GetPacketFactory().Send(handoff);

    // the removal may come at the end of a fade out, the ped must stay visible
    pPed->m_nPedFlags.bFadeOut = false;
    if (pPed->m_pRwClump)
    {
        CVisibilityPlugins::SetClumpAlpha(pPed->m_pRwClump, 255);
    }
}

void CPopulationHandover::HandOverVehicle(CNetworkVehicle* pNetworkVehicle, CNetworkPlayer* pTarget, bool bWithTransform)
{
    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;

    CVehicle* pTrailer = pVehicle->m_pTrailer;

    Packets::Vehicles::VehicleHandoff handoff{};
    pNetworkVehicle->BecomeClone(&handoff);
    handoff.target = pTarget->m_playerId;
    handoff.hasTransform = handoff.hasTransform && bWithTransform;
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
        HandOverVehicle(pNetworkTrailer, pTarget, bWithTransform);
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

bool CPopulationHandover::TryHandOverTrain(CNetworkVehicle* pNetworkVehicle)
{
    CNetworkPlayer* pTarget = FindRemovalTarget(
        pNetworkVehicle->m_pVehicle->GetPosition(), TRAIN_HANDOVER_DISTANCE, pNetworkVehicle->m_nOwnershipChangeTime);
    if (!pTarget)
    {
        return false;
    }

    HandOverVehicle(pNetworkVehicle, pTarget);
    logger::info("train %s handed over to %s instead of removing", to_string(pNetworkVehicle->m_vehicleId).c_str(),
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

bool CPopulationHandover::HasGround(const CVector& position, int nAreaCode)
{
    // the pair CMissionCleanup::CheckIfCollisionHasLoadedForMissionObjects (0x4652D0) waits for before it lets a
    // script entity move: the collision files and the IPLs, whose buildings are the ground. Gone into an interior
    // we keep the collision of the street, not the buildings
    return plugin::CallAndReturn<bool, 0x410CE0, const CVector&, int>(position, nAreaCode) &&  // CColStore::HasCollisionLoaded
           plugin::CallAndReturn<bool, 0x405600, const CVector&, int>(position, nAreaCode);    // CIplStore::HaveIplsLoaded
}

// where to give a mission entity we sync. The script keeps it wherever it is, also far from us: there our game has no
// collision, the entity hangs frozen (m_bIsStaticWaitingForCollision) or falls, and the clone of a player standing next
// to it, which lies on its own collision, is pulled after it by our packets and jumps. A player near it syncs it then;
// a borrowed one goes back to the host, whose scripts drive it, once the host is near again
static CNetworkPlayer* FindMissionTarget(CPhysical* pEntity, unsigned int nOwnershipChangeTime, bool bBorrowed)
{
    constexpr unsigned int HOLD_TIME = 3000;  // no ping-pong right after a change
    constexpr unsigned int AREA_SETTLE_TIME = 5000;
    // well inside the collision the game keeps around a player
    constexpr float MAX_TARGET_DISTANCE = 100.0f;
    constexpr float MAX_HOST_DISTANCE = 50.0f;

    if (CTimer::m_snTimeInMilliseconds - nOwnershipChangeTime < HOLD_TIME)
    {
        return nullptr;
    }

    const CVector& position = pEntity->GetPosition();

    if (bBorrowed)
    {
        for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
        {
            // just out of an interior the host's game is still loading the street: it would fall there and come back
            if (pNetworkPlayer->m_bIsHost && pNetworkPlayer->m_pPed &&
                pNetworkPlayer->m_pPed->m_nAreaCode == pEntity->m_nAreaCode &&
                CTimer::m_snTimeInMilliseconds - pNetworkPlayer->m_nAreaChangeTime >= AREA_SETTLE_TIME &&
                (pNetworkPlayer->m_pPed->GetPosition() - position).Magnitude() < MAX_HOST_DISTANCE)
            {
                return pNetworkPlayer;
            }
        }
    }

    CPlayerPed* pPlayer = FindPlayerPed(0);
    if (pPlayer && pPlayer->m_nAreaCode == pEntity->m_nAreaCode &&
        CPopulationHandover::HasGround(position, pEntity->m_nAreaCode))
    {
        return nullptr;
    }

    // one in an interior has no street under it either
    return CPopulationHandover::FindNearestRemotePlayer(position, MAX_TARGET_DISTANCE, pEntity->m_nAreaCode);
}

// a car the script left where nobody has ground any more (all of us inside): it would fall through the map. Held as
// the game holds a script entity waiting for its collision, until our ground is back or someone near has it
static void HoldForGround(CNetworkVehicle* pNetworkVehicle, bool bHold)
{
    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    if (bHold == pNetworkVehicle->m_bHeldForGround)
    {
        return;
    }

    pNetworkVehicle->m_bHeldForGround = bHold;
    pVehicle->m_bIsStaticWaitingForCollision = bHold;
    if (bHold)
    {
        pVehicle->m_vecMoveSpeed = CVector{};
        pVehicle->m_vecTurnSpeed = CVector{};
    }
    logger::info("mission vehicle %s %s", to_string(pNetworkVehicle->m_vehicleId).c_str(),
        bHold ? "held, no ground under it" : "released, the ground is back");
}

// a player drives it (his game syncs it) or ours sits in it. Only other players as passengers: it goes to one of them
// when our ground is gone, as an empty one (the host in an interior, someone waiting in the car outside)
static bool IsKeptByPlayerInside(CVehicle* pVehicle)
{
    if (pVehicle->m_pDriver && pVehicle->m_pDriver->IsPlayer())
    {
        return true;
    }

    CPlayerPed* pPlayer = FindPlayerPed(0);
    return pPlayer && std::find(std::begin(pVehicle->m_apPassengers), std::end(pVehicle->m_apPassengers), pPlayer) !=
                          std::end(pVehicle->m_apPassengers);
}

static void MigrateMissionEntities()
{
    for (auto pNetworkVehicle : std::vector<CNetworkVehicle*>(CNetworkVehicleManager::m_pVehicles))
    {
        CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
        // someone else syncs it now or drives it: its ground is theirs
        if (pVehicle && pNetworkVehicle->m_bHeldForGround && (!pNetworkVehicle->m_bSyncing || IsKeptByPlayerInside(pVehicle)))
        {
            HoldForGround(pNetworkVehicle, false);
        }

        // a towed one goes with its tractor, a train has its own rules (CTrainSync)
        if (!pVehicle || !pNetworkVehicle->m_bSyncing || pNetworkVehicle->m_nCreatedBy != MISSION_VEHICLE ||
            pNetworkVehicle->m_bTrain || pVehicle->m_pTractor || IsKeptByPlayerInside(pVehicle))
        {
            continue;
        }

        if (auto pTarget =
                FindMissionTarget(pVehicle, pNetworkVehicle->m_nOwnershipChangeTime, pNetworkVehicle->m_bBorrowed))
        {
            // our copy lies wherever our missing ground let it, the target's lies on its own
            HoldForGround(pNetworkVehicle, false);
            CPopulationHandover::HandOverVehicle(pNetworkVehicle, pTarget, false);
            logger::info("mission vehicle %s handed over to %s for its collision",
                to_string(pNetworkVehicle->m_vehicleId).c_str(), to_string(pTarget->m_playerId).c_str());
            continue;
        }

        CPlayerPed* pPlayer = FindPlayerPed(0);
        HoldForGround(pNetworkVehicle, !pPlayer || pPlayer->m_nAreaCode != pVehicle->m_nAreaCode ||
                                           !CPopulationHandover::HasGround(pVehicle->GetPosition(), pVehicle->m_nAreaCode));
    }

    for (auto pNetworkPed : std::vector<CNetworkPed*>(CNetworkPedManager::m_pPeds))
    {
        CPed* pPed = pNetworkPed->m_pPed;
        // one in a vehicle goes with the vehicle
        if (!pPed || !pNetworkPed->m_bSyncing || pNetworkPed->m_nCreatedBy != MISSION_CHAR ||
            pPed->m_nPedFlags.bInVehicle || IsInPlayerGroup(pPed) || pNetworkPed->IsGettingIntoVehicle() ||
            IsDyingOrDead(pPed))
        {
            continue;
        }

        if (auto pTarget = FindMissionTarget(pPed, pNetworkPed->m_nOwnershipChangeTime, pNetworkPed->m_bBorrowed))
        {
            CPopulationHandover::HandOverPed(pNetworkPed, pTarget);
            logger::info("mission ped %s handed over to %s for its collision", to_string(pNetworkPed->m_pedId).c_str(),
                to_string(pTarget->m_playerId).c_str());
        }
    }
}

// someone else's ped that ended up in our player's group follows our player only if its ai runs here
static void ClaimLocalGroup()
{
    CPlayerPed* pPlayer = FindPlayerPed(0);
    if (!pPlayer || !pPlayer->m_pPlayerData)
    {
        return;
    }

    CPedGroupMembership& membership = CPedGroups::ms_groups[pPlayer->m_pPlayerData->m_nPlayerGroup].m_groupMembership;
    for (int i = 0; i < 7; i++)  // 7 is the leader
    {
        CPed* pMember = membership.m_apMembers[i];
        CNetworkPed* pNetworkPed = pMember ? CNetworkPedManager::GetPed(pMember) : nullptr;
        if (pNetworkPed && !pNetworkPed->m_bSyncing && pNetworkPed->m_nCreatedBy == RANDOM_CHAR)
        {
            Packets::Peds::PedRequestOwnership packet{};
            packet.pedid = pNetworkPed->m_pedId;
            GetPacketFactory().Send(packet);
        }
    }
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

    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer->m_pPed && pNetworkPlayer->m_pPed->m_nAreaCode != pNetworkPlayer->m_nLastAreaCode)
        {
            pNetworkPlayer->m_nLastAreaCode = pNetworkPlayer->m_pPed->m_nAreaCode;
            pNetworkPlayer->m_nAreaChangeTime = CTimer::m_snTimeInMilliseconds;
        }
    }

    ClaimLocalGroup();
    MigrateMissionEntities();

    for (auto pNetworkPed : std::vector<CNetworkPed*>(CNetworkPedManager::m_pPeds))
    {
        // on the way into a vehicle (a jacking too): the clones play our enter task, a new syncer would stop it
        if (!pNetworkPed->m_pPed || !GetOwnedAmbientPed(pNetworkPed->m_pPed) || pNetworkPed->IsGettingIntoVehicle())
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
