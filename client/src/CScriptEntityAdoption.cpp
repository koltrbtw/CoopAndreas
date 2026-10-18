#include "stdafx.h"
#include "CScriptEntityAdoption.h"
#include "COpCodeSync.h"
#include "CWorldGridSync.h"
#include "CPopulationHandover.h"

namespace
{
float GetFloatParam(int i)
{
    return *reinterpret_cast<float*>(&ScriptParams[i]);
}

bool IsNear(const CVector& entity, const CVector& position, float fRadius, float fMaxHeight)
{
    // a z of -100 or below: the game puts it on the ground
    return DistanceBetweenPoints(CVector2D(entity), CVector2D(position)) < fRadius &&
           (position.z <= -100.0f || std::fabs(entity.z - position.z) < fMaxHeight);
}
}  // namespace

bool CScriptEntityAdoption::IsScriptSynced(const CRunningScript* pScript)
{
    // the synced scripts of the host (the missions, but not only): the other players get their entities from it
    for (uint32_t i = 0; i < COpCodeSync::ms_iFreeSyncedScript; i++)
    {
        if (strnicmp(COpCodeSync::ms_aszSyncedScripts[i], pScript->m_szName, 7) == 0)
        {
            return true;
        }
    }
    return false;
}

bool CScriptEntityAdoption::IsClone(CPed* pPed)
{
    CNetworkPed* pNetworkPed = pPed ? CNetworkPedManager::GetPed(pPed) : nullptr;
    return pNetworkPed && !pNetworkPed->m_bSyncing;
}

bool CScriptEntityAdoption::IsClone(CVehicle* pVehicle)
{
    CNetworkVehicle* pNetworkVehicle = pVehicle ? CNetworkVehicleManager::GetVehicle(pVehicle) : nullptr;
    return pNetworkVehicle && !pNetworkVehicle->m_bSyncing;
}

int CScriptEntityAdoption::FindPed(int model, const CVector& position, float fRadius)
{
    for (CNetworkPed* pNetworkPed : CNetworkPedManager::m_pPeds)
    {
        CPed* pPed = pNetworkPed->m_pPed;
        if (pPed && !pNetworkPed->m_bSyncing && pNetworkPed->m_nCreatedBy == MISSION_CHAR &&
            pPed->m_nModelIndex == model && pPed->IsAlive() && !pPed->m_nPedFlags.bInVehicle &&
            IsNear(pPed->GetPosition(), position, fRadius, MAX_HEIGHT))
        {
            return CPools::GetPedRef(pPed);
        }
    }
    return -1;
}

int CScriptEntityAdoption::FindVehicle(int model, const CVector& position)
{
    for (CNetworkVehicle* pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
        if (pVehicle && !pNetworkVehicle->m_bSyncing && pNetworkVehicle->m_nCreatedBy == MISSION_VEHICLE &&
            pVehicle->m_nModelIndex == model && pVehicle->m_nStatus != STATUS_WRECKED &&
            IsNear(pVehicle->GetPosition(), position, VEHICLE_RADIUS, MAX_HEIGHT))
        {
            return CPools::GetVehicleRef(pVehicle);
        }
    }
    return -1;
}

int CScriptEntityAdoption::FindOccupant(CVehicle* pVehicle, int model, int seat)
{
    // seat -2: the driver, -1: any passenger seat, else that passenger seat
    for (int i = -1; i < pVehicle->m_nMaxPassengers && i < 8; i++)
    {
        bool bWanted = seat == -2 ? i == -1 : i >= 0 && (seat == -1 || seat == i);
        CPed* pPed = !bWanted ? nullptr : i == -1 ? pVehicle->m_pDriver : pVehicle->m_apPassengers[i];
        if (pPed && pPed->m_nModelIndex == model && IsClone(pPed))
        {
            return CPools::GetPedRef(pPed);
        }
    }
    return -1;
}

bool CScriptEntityAdoption::IsOursToSpawn(const CVector& position)
{
    // an interior: nobody else in it (the sectors of the new area come later); outside: the sector of the spot
    return CGame::currArea != 0 ? CWorldGridSync::CanPopulateInterior(position) : CWorldGridSync::CanSpawnAt(position);
}

CScriptEntityAdoption::eResult CScriptEntityAdoption::WaitOrCreate(
    CRunningScript* pScript, const uint8_t* pCommandIP, const CVector& position, int model)
{
    if (IsOursToSpawn(position))
    {
        ms_waits.erase(pScript);
        return eResult::NOT_HANDLED;
    }

    SWait& wait = ms_waits[pScript];
    if (wait.m_pIP != pCommandIP)
    {
        wait.m_pIP = pCommandIP;
        wait.m_nStart = CTimer::m_snTimeInMilliseconds;
    }
    if (CTimer::m_snTimeInMilliseconds - wait.m_nStart < MAX_WAIT)
    {
        return eResult::WAIT;
    }

    ms_waits.erase(pScript);
    logger::info("script %.8s: no clone of %d at %.1f %.1f %.1f came, our own one", pScript->m_szName, model,
        position.x, position.y, position.z);
    return eResult::NOT_HANDLED;
}

CScriptEntityAdoption::eResult CScriptEntityAdoption::ProcessCommand(uint16_t command, CRunningScript* pScript)
{
    command &= 0x7FFF;
    if ((command != COMMAND_CREATE_CHAR && command != COMMAND_CREATE_CAR && command != COMMAND_CREATE_CHAR_INSIDE_CAR &&
            command != COMMAND_CREATE_CHAR_AS_PASSENGER && command != COMMAND_DELETE_CHAR &&
            command != COMMAND_DELETE_CAR && command != COMMAND_MARK_CHAR_AS_NO_LONGER_NEEDED &&
            command != COMMAND_MARK_CAR_AS_NO_LONGER_NEEDED) ||
        !CNetwork::m_bAuthenticated || CNetworkPlayerManager::m_pPlayers.empty() || pScript->m_bIsMission ||
        IsScriptSynced(pScript))
    {
        return eResult::NOT_HANDLED;
    }

    uint8_t* pCommandIP = pScript->m_pCurrentIP;
    pScript->m_pCurrentIP += 2;

    switch (command)
    {
    case COMMAND_DELETE_CHAR:  // delete_char {handle}
    case COMMAND_MARK_CHAR_AS_NO_LONGER_NEEDED:
    case COMMAND_DELETE_CAR:  // delete_car {handle}
    case COMMAND_MARK_CAR_AS_NO_LONGER_NEEDED:
    {
        // the game would delete a clone here, or let the population remove it: the copy of the syncer was gone for
        // us, and our script made one of its own the next time
        pScript->CollectParameters(1);
        bool bChar = command == COMMAND_DELETE_CHAR || command == COMMAND_MARK_CHAR_AS_NO_LONGER_NEEDED;
        CPed* pPed = bChar ? CPools::GetPed(ScriptParams[0]) : nullptr;
        CVehicle* pVehicle = bChar ? nullptr : CPools::GetVehicle(ScriptParams[0]);
        if (bChar ? IsClone(pPed) : IsClone(pVehicle))
        {
            return eResult::HANDLED;
        }

        // ours, and the script of a player nearer to it has it as a clone: it stays, theirs (a no longer needed one
        // too: our population code removed it later, for everyone)
        CEntity* pEntity = bChar ? static_cast<CEntity*>(pPed) : pVehicle;
        CPlayerPed* pLocalPlayer = FindPlayerPed(0);
        CNetworkPlayer* pTarget = nullptr;
        if (pEntity && pLocalPlayer)
        {
            const CVector& position = pEntity->GetPosition();
            float fOurDistance = DistanceBetweenPoints(pLocalPlayer->GetPosition(), position);
            pTarget = CPopulationHandover::FindNearestRemotePlayer(position, std::min(HANDOVER_RADIUS, fOurDistance));
        }
        CNetworkPed* pNetworkPed = pPed ? CNetworkPedManager::GetPed(pPed) : nullptr;
        CNetworkVehicle* pNetworkVehicle = pVehicle ? CNetworkVehicleManager::GetVehicle(pVehicle) : nullptr;
        if (pTarget && pNetworkPed && pNetworkPed->m_bSyncing && pPed->IsAlive() && !pPed->m_nPedFlags.bInVehicle)
        {
            CPopulationHandover::HandOverPed(pNetworkPed, pTarget);
            logger::info("script %.8s: ped %s handed over to %s instead of letting it go", pScript->m_szName,
                to_string(pNetworkPed->m_pedId).c_str(), to_string(pTarget->m_playerId).c_str());
            return eResult::HANDLED;
        }
        if (pTarget && pNetworkVehicle && pNetworkVehicle->m_bSyncing && !pNetworkVehicle->m_bTrain &&
            pVehicle->m_nStatus != STATUS_WRECKED && pLocalPlayer->m_pVehicle != pVehicle)
        {
            CPopulationHandover::HandOverVehicle(pNetworkVehicle, pTarget);
            logger::info("script %.8s: vehicle %s handed over to %s instead of letting it go", pScript->m_szName,
                to_string(pNetworkVehicle->m_vehicleId).c_str(), to_string(pTarget->m_playerId).c_str());
            return eResult::HANDLED;
        }
        break;
    }
    case COMMAND_CREATE_CHAR:  // create_char {pedType} {model} {x} {y} {z} [handle]
    case COMMAND_CREATE_CAR:   // create_car {model} {x} {y} {z} [handle]
    {
        bool bChar = command == COMMAND_CREATE_CHAR;
        pScript->CollectParameters(bChar ? 5 : 4);
        int i = bChar ? 1 : 0;
        int model = ScriptParams[i];
        CVector position(GetFloatParam(i + 1), GetFloatParam(i + 2), GetFloatParam(i + 3));
        int handle = bChar ? FindPed(model, position, PED_RADIUS) : FindVehicle(model, position);
        if (handle != -1)
        {
            ScriptParams[0] = handle;
            pScript->StoreParameters(1);
            ms_waits.erase(pScript);
            logger::info("script %.8s: %s %d at %.1f %.1f %.1f is the clone of another player's one",
                pScript->m_szName, bChar ? "ped" : "vehicle", model, position.x, position.y, position.z);
            return eResult::HANDLED;
        }

        pScript->m_pCurrentIP = pCommandIP;
        return WaitOrCreate(pScript, pCommandIP, position, model);
    }
    case COMMAND_CREATE_CHAR_INSIDE_CAR:    // create_char_inside_car {car} {pedType} {model} [handle]
    case COMMAND_CREATE_CHAR_AS_PASSENGER:  // create_char_as_passenger {car} {pedType} {model} {seat} [handle]
    {
        bool bDriver = command == COMMAND_CREATE_CHAR_INSIDE_CAR;
        pScript->CollectParameters(bDriver ? 3 : 4);
        CVehicle* pVehicle = CPools::GetVehicle(ScriptParams[0]);
        int model = ScriptParams[2];
        if (!IsClone(pVehicle))
        {
            break;  // our vehicle, our ped in it
        }

        // the vehicle is a clone: its syncer's script put its own ped there, which may have got out since
        int handle = FindOccupant(pVehicle, model, bDriver ? -2 : ScriptParams[3]);
        if (handle == -1)
        {
            handle = FindPed(model, pVehicle->GetPosition(), OCCUPANT_RADIUS);
        }
        if (handle != -1)
        {
            ScriptParams[0] = handle;
            pScript->StoreParameters(1);
            ms_waits.erase(pScript);
            logger::info("script %.8s: ped %d in a vehicle is the clone of another player's one", pScript->m_szName,
                model);
            return eResult::HANDLED;
        }

        pScript->m_pCurrentIP = pCommandIP;
        return WaitOrCreate(pScript, pCommandIP, pVehicle->GetPosition(), model);
    }
    }

    pScript->m_pCurrentIP = pCommandIP;
    return eResult::NOT_HANDLED;
}
