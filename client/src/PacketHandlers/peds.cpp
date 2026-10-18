#include "network/packets/peds.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "CPedAnimSync.h"
#include <CCarEnterExit.h>
#include <CTaskSimpleCarSetPedInAsPassenger.h>

PACKET_HANDLER(ePacketType::PED_SPAWN, Packets::Peds::PedSpawn* pPedSpawn)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("PED SPAWN %s %d %.1f %.1f %.1f %d %d", to_string(pPedSpawn->pedid).c_str(), pPedSpawn->modelId,
        pPedSpawn->pos.x, pPedSpawn->pos.y, pPedSpawn->pos.z, pPedSpawn->pedType, pPedSpawn->createdBy);
#endif

    // the server sends a spawn whenever the ped comes in our scope, never twice on purpose
    if (CNetworkPedManager::GetPed(pPedSpawn->pedid))
    {
        return;
    }

    CNetworkPedManager::HandleSpawn(*pPedSpawn);
}

PACKET_HANDLER(ePacketType::PED_REMOVE, Packets::Peds::PedRemove* pPedRemove)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("PED REMOVE %s", to_string(pPedRemove->pedid).c_str());
#endif

    CNetworkPedManager::CancelPendingSpawn(pPedRemove->pedid);

    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedRemove->pedid);
    if (pNetworkPed)
    {
        CNetworkPedManager::Remove(pNetworkPed);
        delete pNetworkPed;
    }
}

PACKET_HANDLER(ePacketType::ASSIGN_PED, Packets::Peds::AssignPedSyncer* pAssignPedSyncer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pAssignPedSyncer->pedid);

    if (!pNetworkPed || pNetworkPed->m_bSyncing == pAssignPedSyncer->isSyncer)
    {
        return;
    }

#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("%s PED %s", pAssignPedSyncer->isSyncer ? "SYNCING" : "NOT SYNCING",
        to_string(pAssignPedSyncer->pedid).c_str());
#endif

    if (pAssignPedSyncer->isSyncer)
    {
        pNetworkPed->BecomeSyncer(false);
    }
    else
    {
        pNetworkPed->BecomeClone(false);
    }
}

PACKET_HANDLER(ePacketType::PED_REQUEST_OWNERSHIP, Packets::Peds::PedRequestOwnership* pRequest)
{
    // someone asked the server for our ped
    Packets::Peds::PedHandoff handoff{};
    handoff.pedid = pRequest->pedid;

    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pRequest->pedid);

    // peds in vehicles go together with the vehicle, see VEHICLE_REQUEST_OWNERSHIP; a dying or dead one stays
    // (its die task would be flushed, the body would get an ambient life)
    if (pNetworkPed && pNetworkPed->m_bSyncing && pNetworkPed->m_pPed && !pNetworkPed->m_pPed->m_nPedFlags.bInVehicle &&
        pNetworkPed->m_pPed->IsAlive() && pNetworkPed->m_pPed->m_ePedState != PEDSTATE_DIE_BY_STEALTH)
    {
        handoff.released = true;
        handoff.pos = pNetworkPed->m_pPed->GetPosition();
        handoff.heading = pNetworkPed->m_pPed->m_fCurrentRotation;
        pNetworkPed->BecomeClone(true);
        logger::info("ped %s released", to_string(handoff.pedid).c_str());
    }

    GetPacketFactory().Send(handoff);
}

PACKET_HANDLER(ePacketType::PED_HANDOFF, Packets::Peds::PedHandoff* pHandoff)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pHandoff->pedid);
    if (!pNetworkPed)
    {
        // its clone may still wait for the model, it becomes ours once created
        CNetworkPedManager::DeferHandoff(*pHandoff);
        return;
    }

    pNetworkPed->ApplyHandoff(*pHandoff);
}

// between the ped entering a vehicle here and the syncer's on foot position
static constexpr float PED_ENTER_VEHICLE_GIVE_UP_DISTANCE = 8.0f;

PACKET_HANDLER(ePacketType::PED_VEHICLE_ENTER, Packets::Peds::PedVehicleEnter* pPedVehicleEnter)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedVehicleEnter->pedid);
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedVehicleEnter->vehicleid);
    if (!pNetworkPed || pNetworkPed->m_bSyncing || !pNetworkPed->m_pPed || !pNetworkVehicle || !pNetworkVehicle->m_pVehicle)
    {
        return;
    }

    pNetworkPed->EnterVehicle(pNetworkVehicle->m_pVehicle, pPedVehicleEnter->passenger, pPedVehicleEnter->seat,
        pPedVehicleEnter->senderPlayerid.value);
}

PACKET_HANDLER(ePacketType::PED_ONFOOT, Packets::Peds::PedOnFoot* pPedOnFoot)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedOnFoot->pedid);

    if (!pNetworkPed)
    {
        return;
    }

    CPed* pPed = pNetworkPed->m_pPed;
    if (!pPed)
    {
        return;
    }

    CVehicle* pVehicle = pPed->m_pVehicle;
    if (pVehicle && pPed->m_nPedFlags.bInVehicle && pVehicle->IsVTableValid())
    {
        // our enter task got it in before the syncer's did: its last on foot updates are still on the way
        if (pNetworkPed->HasJustEnteredVehicle())
        {
            return;
        }

        // our copy of the leave task (or a player jacking it here) gets it out on its own, the position of the
        // syncer would fight it; the syncer far away gave it up
        if (pNetworkPed->IsLeavingVehicle(pVehicle) &&
            (pPed->GetPosition() - pPedOnFoot->pos).Magnitude() <= PED_ENTER_VEHICLE_GIVE_UP_DISTANCE)
        {
            pNetworkPed->ApplyWeaponSnapshot(pPedOnFoot->weaponSnapshot);
            return;
        }

        // the syncer's exit that waited for it has the way it got out (a fall), else a plain warp out
        if (!CVehicleExitSync::ApplyPendingPedExit(pNetworkPed))
        {
            pNetworkPed->RemoveFromVehicle(pVehicle);
        }
        pNetworkPed->m_nLeaveVehicleTime = 0;
    }

    // on the way in our copy of the enter task walks it to the door and plays the get in (or the jacking), the
    // position and the anims of the syncer would fight it; the syncer far away gave the enter up
    CNetworkVehicle* pEnterVehicle = CNetworkVehicleManager::GetVehicle(pNetworkPed->m_enterVehicleId);
    if (pEnterVehicle && pNetworkPed->IsEnteringVehicle(pEnterVehicle->m_pVehicle))
    {
        if ((pPed->GetPosition() - pPedOnFoot->pos).Magnitude() <= PED_ENTER_VEHICLE_GIVE_UP_DISTANCE)
        {
            pNetworkPed->ApplyWeaponSnapshot(pPedOnFoot->weaponSnapshot);
            pNetworkPed->m_fHealth = pPed->m_fHealth = pPedOnFoot->healthSnapshot.iHealth;
            pPed->m_fArmour = pPedOnFoot->healthSnapshot.iArmour;
            return;
        }
        logger::info("ped %s gave its enter up: %.1f m from the syncer's one", to_string(pNetworkPed->m_pedId).c_str(),
            (pPed->GetPosition() - pPedOnFoot->pos).Magnitude());
        pPed->m_pIntelligence->FlushImmediately(false);
        pNetworkPed->m_nEnterVehicleTime = 0;
    }

    pNetworkPed->ApplyWeaponSnapshot(pPedOnFoot->weaponSnapshot);
    pNetworkPed->SetTargetPosition(pPedOnFoot->pos, pPedOnFoot->velocity);

    pNetworkPed->m_fCurrentRotation = pPed->m_fCurrentRotation = pPedOnFoot->currentRotation.m_angle;
    pNetworkPed->m_fAimingRotation = pPed->m_fAimingRotation = pPedOnFoot->aimingRotation.m_angle;
    pNetworkPed->m_fLookDirection = pPed->m_fLookDirection = pPedOnFoot->lookDirection.m_angle;

    pNetworkPed->m_fHealth = pPed->m_fHealth = pPedOnFoot->healthSnapshot.iHealth;
    pPed->m_fArmour = pPedOnFoot->healthSnapshot.iArmour;

    pNetworkPed->m_nMoveState = pPedOnFoot->moveState;

    if (CUtil::IsDucked(pPed) != pPedOnFoot->bDucked)
    {
        CTaskSimpleDuckToggle task = CTaskSimpleDuckToggle(pPedOnFoot->bDucked);
        task.ProcessPed(pPed);
    }

    // TODO reimplement ped aim sync
    if (pPedOnFoot->bAiming)
    {
        CTaskSimpleUseGun* useGun = pPed->m_pIntelligence->GetTaskUseGun();
        if (!useGun)
        {
            auto* taskUseGun = new CTaskSimpleUseGun(pPed->m_pTargetedObject, CVector(0.0f, 0.0f, 0.f), 1, 1, false);
            pPed->m_pIntelligence->m_TaskMgr.SetTaskSecondary(taskUseGun, TASK_SECONDARY_ATTACK);
        }

        useGun = pPed->m_pIntelligence->GetTaskUseGun();

        if (useGun)
        {
            useGun->m_vecTarget = pPedOnFoot->weaponAim;
        }
    }
    else
    {
        if (auto useGun = pPed->m_pIntelligence->GetTaskUseGun())
        {
            useGun->MakeAbortable(pPed, ABORT_PRIORITY_URGENT, nullptr);
        }
    }

    pPed->m_nFightingStyle = pPedOnFoot->fightingStyle;

    CPedAnimSync::Apply(pPed, pPedOnFoot->anims, pPedOnFoot->animCount);
    CPedStateSync::Apply(pPed, pPedOnFoot->stateFlags, pNetworkPed->m_nLastStateFlags);
}

PACKET_HANDLER(ePacketType::PED_DRIVER_UPDATE, Packets::Peds::PedDriverUpdate* pPedDriverUpdate)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedDriverUpdate->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }
    pNetworkVehicle->OnSyncerPacket();

    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    if (pVehicle == nullptr || !pVehicle->IsVTableValid())
    {
        return;
    }

    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedDriverUpdate->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    CPed* pPed = pNetworkPed->m_pPed;
    if (pPed == nullptr || !pPed->IsVTableValid())
    {
        return;
    }

    // sent before the syncer's ped got out
    if (pNetworkPed->HasJustLeftVehicle(pPedDriverUpdate->vehicleid))
    {
        return;
    }

    if (pPed->m_pVehicle != pVehicle || !pPed->m_nPedFlags.bInVehicle)
    {
        // our enter task lags behind the syncer's, a warp would cut its get in (or jacking) anim; the vehicle waits
        // for it too, the syncer's ped may drive off already and our copy would walk after a moving door
        if (pNetworkPed->IsEnteringVehicle(pVehicle))
        {
            return;
        }
        pNetworkPed->WarpIntoVehicleDriver(pVehicle);
    }
    pNetworkVehicle->SetTargetTransform(pPedDriverUpdate->pos, pPedDriverUpdate->roll, pPedDriverUpdate->rot,
        pPedDriverUpdate->velocity, pPedDriverUpdate->turnSpeed, pPedDriverUpdate->sendTime);
    pNetworkPed->m_vecVelocity = pPedDriverUpdate->velocity;
    // an npc heli or plane: the clone's pedals (VehicleHooks) mean nothing to its flight, it sagged between updates
    // and the target pulled it up again
    bool bAircraft = pVehicle->m_nVehicleSubType == VEHICLE_HELI || pVehicle->m_nVehicleSubType == VEHICLE_PLANE;
    pNetworkVehicle->m_bKeepSpeed = pPedDriverUpdate->onRails || bAircraft;
    pNetworkVehicle->m_vecKeptMoveSpeed = pPedDriverUpdate->velocity;
    pNetworkVehicle->m_vecKeptTurnSpeed = pPedDriverUpdate->turnSpeed;
    pNetworkVehicle->m_bHasRotorSpeed = pPedDriverUpdate->hasRotorSpeed;
    pNetworkVehicle->ApplyExtras(pPedDriverUpdate->extras);
    pNetworkVehicle->m_fRotorSpeed = pPedDriverUpdate->rotorSpeed;

    pNetworkPed->ApplyWeaponSnapshot(pPedDriverUpdate->pedWeapon);

    pNetworkPed->m_fHealth = pPed->m_fHealth = pPedDriverUpdate->pedHealth.iHealth;
    pPed->m_fArmour = pPedDriverUpdate->pedHealth.iArmour;

    pVehicle->m_nPrimaryColor = pPedDriverUpdate->color1;
    pVehicle->m_nSecondaryColor = pPedDriverUpdate->color2;

    pVehicle->m_fHealth = pPedDriverUpdate->health;

    if (pNetworkVehicle->m_nPaintJob != pPedDriverUpdate->paintjob)
    {
        pVehicle->SetRemap(pPedDriverUpdate->paintjob);
    }

    if (pVehicle->m_nVehicleType == VEHICLE_BIKE)
    {
        CBike* pBike = (CBike*)pVehicle;
        pBike->m_rideAnimData.m_fDesiredLeanAngle = pPedDriverUpdate->bikeLean;
    }
    if (pVehicle->m_nVehicleSubType == VEHICLE_PLANE)
    {
        pNetworkVehicle->m_bHasGear = true;
        pNetworkVehicle->m_bGearUp = pPedDriverUpdate->planeGearState > 0.0f;
    }
    if (pVehicle->m_nVehicleSubType == VEHICLE_BMX)
    {
        CBmx* pBmx = (CBmx*)pVehicle;
        pBmx->m_fControlPedaling = pPedDriverUpdate->controlPedaling;
    }
    if (pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
    {
        ((CAutomobile*)pVehicle)->m_wMiscComponentAngle = pPedDriverUpdate->miscComponentAngle;
    }
    pVehicle->m_eDoorLock = pPedDriverUpdate->locked;
    pNetworkVehicle->ApplyTrailer(pPedDriverUpdate->trailerid);

    pVehicle->m_fGasPedal = pNetworkPed->m_fGasPedal = pPedDriverUpdate->gasPedal;
    pVehicle->m_fBreakPedal = pNetworkPed->m_fBreakPedal = pPedDriverUpdate->breakPedal;
    pVehicle->m_fSteerAngle = pNetworkPed->m_fSteerAngle = pPedDriverUpdate->steerAngle;

    pVehicle->m_nVehicleFlags.bSirenOrAlarm = pPedDriverUpdate->siren;
    pVehicle->m_nHornCounter = pPedDriverUpdate->hornCounter;
}

PACKET_HANDLER(ePacketType::PED_PASSENGER_UPDATE, Packets::Peds::PedPassengerSync* pPedPassengerSync)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedPassengerSync->vehicleid);
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedPassengerSync->pedid);

    if (pNetworkVehicle == nullptr || pNetworkPed == nullptr)
        return;

    if (pNetworkVehicle->m_pVehicle == nullptr)
        return;

    if (pNetworkPed->m_pPed == nullptr)
        return;

    if (!pNetworkVehicle->m_pVehicle->IsVTableValid() || !pNetworkPed->m_pPed->IsVTableValid())
        return;

    // sent before the syncer's ped got out
    if (pNetworkPed->HasJustLeftVehicle(pPedPassengerSync->vehicleid))
    {
        return;
    }

    if ((!pNetworkPed->m_pPed->m_nPedFlags.bInVehicle || pNetworkVehicle->m_pVehicle->m_pDriver == pNetworkPed->m_pPed) &&
        !pNetworkPed->IsEnteringVehicle(pNetworkVehicle->m_pVehicle))
    {
        pNetworkPed->WarpIntoVehiclePassenger(pNetworkVehicle->m_pVehicle, pPedPassengerSync->seatid);
    }

    pNetworkPed->ApplyWeaponSnapshot(pPedPassengerSync->weaponSnapshot);

    pNetworkPed->m_fHealth = pNetworkPed->m_pPed->m_fHealth = pPedPassengerSync->healthSnapshot.iHealth;
    pNetworkPed->m_pPed->m_fArmour = pPedPassengerSync->healthSnapshot.iArmour;
}

PACKET_HANDLER(ePacketType::PED_SHOT_SYNC, Packets::Peds::PedShotSync* pPedShotSync)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedShotSync->pedid);

    if (pNetworkPed && pNetworkPed->m_pPed)
    {
        if (pNetworkPed->m_pPed->GetWeapon().m_eWeaponType != pPedShotSync->weaponType)
        {
            pNetworkPed->m_pPed->SetCurrentWeapon(pPedShotSync->weaponType);
        }
        pNetworkPed->m_pPed->GetWeapon().Fire(
            pNetworkPed->m_pPed, &pPedShotSync->origin, &pPedShotSync->effect, nullptr, &pPedShotSync->target, nullptr);
    }
}

PACKET_HANDLER(ePacketType::PED_SAY, Packets::Peds::PedSay* pPedSay)
{
    // CChat::AddMessage("PedSay %d %d %d %d %d", pPedSay->phraseId, pPedSay->startTimeDelay, pPedSay->overrideSilence,
    // pPedSay->isForceAudible, pPedSay->isFrontEnd);

    CPed* pPed = (CPed*)pPedSay->entity.GetEntity();
    if (pPed)
    {
        // CAEPedSpeechAudioEntity::AddSayEvent
        plugin::CallMethodAndReturn<int16_t, 0x4E6550, CAEPedSpeechAudioEntity*, int, int16_t, uint32_t, float, bool,
            bool, bool>(&pPed->m_pedSpeech, AE_SPEECH_PED, pPedSay->phraseId, pPedSay->startTimeDelay, 1.0f,
            pPedSay->overrideSilence, pPedSay->isForceAudible, pPedSay->isFrontEnd);
    }
}


PACKET_HANDLER(ePacketType::PED_VEHICLE_EXIT, Packets::Peds::PedVehicleExit* pPedVehicleExit)
{
    CVehicleExitSync::HandlePedPacket(*pPedVehicleExit);
}

PACKET_HANDLER(ePacketType::PED_DIED, Packets::Peds::PedDied* pPedDied)
{
    CPedDeathSync::HandleDied(*pPedDied);
}

PACKET_HANDLER(ePacketType::PED_STEALTH_KILL, Packets::Peds::PedStealthKill* pPedStealthKill)
{
    CPedDeathSync::HandleStealthKill(*pPedStealthKill);
}

PACKET_HANDLER(ePacketType::PED_RESET_ALL_CLAIMS, Packets::Peds::PedResetAllClaims* pPedResetAllClaims)
{
    if (auto pNetworkPed = CNetworkPedManager::GetPed(pPedResetAllClaims->pedid))
    {
        if (!pNetworkPed->m_bSyncing)
        {
            pNetworkPed->m_bClaimOnRelease = false;
        }
    }
}
