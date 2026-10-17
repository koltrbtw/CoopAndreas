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

bool CPopulationHandover::TryHandOverPed(CPed* pPed)
{
    if (!CNetwork::m_bAuthenticated)
    {
        return false;
    }

    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPed);
    if (!pNetworkPed || !pNetworkPed->m_bSyncing || pNetworkPed->m_nCreatedBy != RANDOM_CHAR ||
        pPed->m_nPedFlags.bInVehicle || pPed->m_fHealth <= 0.0f)
    {
        return false;
    }

    CNetworkPlayer* pTarget = FindNearestRemotePlayer(pPed->GetPosition(), PED_HANDOVER_DISTANCE);
    if (!pTarget)
    {
        return false;
    }

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

    logger::info("ped %s handed over to %s instead of removing", to_string(handoff.pedid).c_str(),
        to_string(handoff.target).c_str());
    return true;
}

bool CPopulationHandover::TryHandOverVehicle(CVehicle* pVehicle)
{
    if (!CNetwork::m_bAuthenticated)
    {
        return false;
    }

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle);
    if (!pNetworkVehicle || !pNetworkVehicle->m_bSyncing || pNetworkVehicle->m_nCreatedBy == MISSION_VEHICLE ||
        pVehicle->m_nStatus == STATUS_WRECKED)
    {
        return false;
    }

    CPlayerPed* pLocalPlayer = FindPlayerPed(0);
    if (pLocalPlayer && pLocalPlayer->m_pVehicle == pVehicle)
    {
        return false;
    }

    CNetworkPlayer* pTarget = FindNearestRemotePlayer(pVehicle->GetPosition(), VEHICLE_HANDOVER_DISTANCE);
    if (!pTarget)
    {
        return false;
    }

    Packets::Vehicles::VehicleHandoff handoff{};
    pNetworkVehicle->BecomeClone(&handoff);
    handoff.target = pTarget->m_playerId;
    GetPacketFactory().Send(handoff);

    pVehicle->m_nVehicleFlags.bFadeOut = false;
    if (pVehicle->m_pRwClump)
    {
        CVisibilityPlugins::SetClumpAlpha(pVehicle->m_pRwClump, 255);
    }

    logger::info("vehicle %s handed over to %s instead of removing, with %d npc peds",
        to_string(handoff.vehicleid).c_str(), to_string(handoff.target).c_str(), handoff.occupantCount);
    return true;
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
