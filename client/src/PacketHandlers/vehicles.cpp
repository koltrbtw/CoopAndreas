#include "network/packets/vehicles.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include <CCarEnterExit.h>
#include <CTaskSimpleCarSetPedOut.h>
#include <Debug/CDebugOwnership.h>
#include <CTrainSync.h>

PACKET_HANDLER(ePacketType::VEHICLE_SPAWN, Packets::Vehicles::VehicleSpawn* pVehicleSpawn)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("VEHICLE SPAWN %s %d %.1f %.1f %.1f %.1f", to_string(pVehicleSpawn->vehicleid).c_str(),
        pVehicleSpawn->modelid, pVehicleSpawn->pos.x, pVehicleSpawn->pos.y, pVehicleSpawn->pos.z,
        pVehicleSpawn->rot.m_angle);
#endif

    // the server sends a spawn whenever the vehicle comes in our scope, never twice on purpose
    if (CNetworkVehicleManager::GetVehicle(pVehicleSpawn->vehicleid))
    {
        return;
    }

    // its creator can't have two at one spot (CCarGenerator::CheckForBlockage): a silent old one is a clone it forgot
    if (CNetworkVehicle* pSame = CNetworkVehicleManager::FindSameClone(pVehicleSpawn->vehicleid, pVehicleSpawn->modelid,
            pVehicleSpawn->pos))
    {
        CNetworkVehicleManager::DropClone(pSame, "a new one of its creator at the same spot");
    }

    CNetworkVehicle* pNetworkVehicle =
        new CNetworkVehicle(pVehicleSpawn->vehicleid, pVehicleSpawn->modelid, pVehicleSpawn->pos,
            pVehicleSpawn->rot.m_angle, pVehicleSpawn->color1, pVehicleSpawn->color2, pVehicleSpawn->createdBy,
            pVehicleSpawn->extras);
    CNetworkVehicleManager::Add(pNetworkVehicle);
}

PACKET_HANDLER(ePacketType::TRAIN_SPAWN, Packets::Vehicles::TrainSpawn* pTrainSpawn)
{
    CTrainSync::HandleSpawn(pTrainSpawn);
}

PACKET_HANDLER(ePacketType::TRAIN_UPDATE, Packets::Vehicles::TrainUpdate* pTrainUpdate)
{
    CTrainSync::HandleUpdate(pTrainUpdate);
}

PACKET_HANDLER(ePacketType::VEHICLE_REMOVE, Packets::Vehicles::VehicleRemove* pVehicleRemove)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("VEHICLE REMOVE %s", to_string(pVehicleRemove->vehicleid).c_str());
#endif
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleRemove->vehicleid);
    if (pNetworkVehicle)
    {
        CNetworkVehicleManager::Remove(pNetworkVehicle);
        delete pNetworkVehicle;
    }
}

// a parked clone gets a zero speed every update and soon skips its physics (m_nFakePhysics, see WakeFloatingVehicle in
// CNetworkVehicleManager.cpp), then nothing moves its RW frame to the matrix anymore: it was drawn where it was
// created, above the spot the syncer's car had dropped to, until someone got in
static void SetStandingTransform(CNetworkVehicle* pNetworkVehicle, const Packets::Vehicles::VehicleIdleUpdate& update)
{
    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    pNetworkVehicle->m_bHasTarget = false;

    CVector previousPos = pVehicle->m_matrix->pos;
    pVehicle->m_matrix->pos = update.pos;
    pVehicle->m_matrix->right = update.roll;
    pVehicle->m_matrix->up = update.rot;
    pVehicle->UpdateRwMatrix();
    pVehicle->UpdateRwFrame();
    if (DistanceBetweenPoints(previousPos, pVehicle->m_matrix->pos) > 0.05f)
    {
        pVehicle->RemoveAndAdd();  // the world sectors, for the collision
    }

    pVehicle->m_vecMoveSpeed = update.velocity;
    pVehicle->m_vecTurnSpeed = update.turnSpeed;
}

PACKET_HANDLER(ePacketType::VEHICLE_IDLE_UPDATE, Packets::Vehicles::VehicleIdleUpdate* pVehicleIdleUpdate)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleIdleUpdate->vehicleid);

    if (pNetworkVehicle == nullptr)
        return;

    pNetworkVehicle->OnSyncerPacket();

    if (!pNetworkVehicle->m_pVehicle || pNetworkVehicle->m_bTrain)
        return;

    if (pNetworkVehicle->m_pVehicle->m_matrix == nullptr)
        return;

    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    pNetworkVehicle->m_bHasRotorSpeed = pVehicleIdleUpdate->hasRotorSpeed;
    pNetworkVehicle->ApplyExtras(pVehicleIdleUpdate->extras);
    pNetworkVehicle->m_fRotorSpeed = pVehicleIdleUpdate->rotorSpeed;

    // rolling, pushed, falling: like a driven one; a standing one is set as it is, it may skip its physics
    if (pVehicleIdleUpdate->velocity.Magnitude() > 0.001f || pVehicle->m_vecMoveSpeed.Magnitude() > 0.001f)
    {
        pNetworkVehicle->SetTargetTransform(pVehicleIdleUpdate->pos, pVehicleIdleUpdate->roll, pVehicleIdleUpdate->rot,
            pVehicleIdleUpdate->velocity, pVehicleIdleUpdate->turnSpeed, pVehicleIdleUpdate->sendTime);
    }
    else
    {
        SetStandingTransform(pNetworkVehicle, *pVehicleIdleUpdate);
    }

    pNetworkVehicle->m_pVehicle->m_nPrimaryColor = pVehicleIdleUpdate->color1;
    pNetworkVehicle->m_pVehicle->m_nSecondaryColor = pVehicleIdleUpdate->color2;
    pNetworkVehicle->m_pVehicle->m_fHealth = pVehicleIdleUpdate->health;

    if (pNetworkVehicle->m_pVehicle->GetRemapIndex() != pVehicleIdleUpdate->paintjob)
        pNetworkVehicle->m_pVehicle->SetRemap(pVehicleIdleUpdate->paintjob);

    if (CUtil::GetVehicleType(pNetworkVehicle->m_pVehicle) == eVehicleType::VEHICLE_PLANE)
    {
        pNetworkVehicle->m_bHasGear = true;
        pNetworkVehicle->m_bGearUp = pVehicleIdleUpdate->planeGearState > 0.0f;
    }

    pNetworkVehicle->m_pVehicle->m_eDoorLock = pVehicleIdleUpdate->locked;
}

PACKET_HANDLER(ePacketType::VEHICLE_DRIVER_UPDATE, Packets::Vehicles::VehicleDriverUpdate* pVehicleDriverUpdate)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleDriverUpdate->vehicleid);
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pVehicleDriverUpdate->senderPlayerid);

    if (pNetworkVehicle == nullptr || pNetworkPlayer == nullptr)
        return;

    pNetworkVehicle->OnSyncerPacket();
    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    if (!pVehicle->IsVTableValid())
        return;

    // sent before the sender got out; or the sender is still being dragged out while our copy of the jacker is done
    if (pNetworkPlayer->HasJustLeftVehicle(pVehicleDriverUpdate->vehicleid) ||
        (pNetworkPlayer->IsJackAllowed() && !pNetworkPlayer->m_pPed->m_nPedFlags.bInVehicle))
        return;

    if (pNetworkPlayer->m_pPed->m_pVehicle != pVehicle || !pNetworkPlayer->m_pPed->m_nPedFlags.bInVehicle)
    {
        // our enter task lags behind the sender's, a warp would cut its get in (or jacking) anim; the vehicle waits
        // for it too, the sender may drive off already and our copy would walk after a moving door
        if (pNetworkPlayer->IsEnteringVehicle(pVehicle))
        {
            return;
        }
        pNetworkPlayer->WarpIntoVehicleDriver(pVehicle);
    }

    // the sender sits: an on foot update from now on is a real exit (a jacking sends no VEHICLE_EXIT)
    if (!pNetworkPlayer->IsEnteringVehicle(pVehicle))
    {
        pNetworkPlayer->m_nEnterVehicleTime = 0;
    }

    // a train is put on its track by TRAIN_UPDATE, a matrix would only fight CTrain::ProcessControl
    if (!pNetworkVehicle->m_bTrain)
    {
        pNetworkVehicle->SetTargetTransform(pVehicleDriverUpdate->pos, pVehicleDriverUpdate->roll,
            pVehicleDriverUpdate->rot, pVehicleDriverUpdate->velocity, pVehicleDriverUpdate->turnSpeed,
            pVehicleDriverUpdate->sendTime);
    }
    pNetworkVehicle->m_bHasRotorSpeed = pVehicleDriverUpdate->hasRotorSpeed;
    pNetworkVehicle->ApplyExtras(pVehicleDriverUpdate->extras);
    pNetworkVehicle->m_fRotorSpeed = pVehicleDriverUpdate->rotorSpeed;

    pNetworkPlayer->ApplyWeaponSnapshot(pVehicleDriverUpdate->playerWeapon);
    pNetworkPlayer->m_onFootSnapshotInterpolated.healthSnapshot = pVehicleDriverUpdate->playerHealth;
    pNetworkPlayer->m_onFootSnapshotInterpolated.iBreath = 100;  // breath is only sent on foot
    pNetworkPlayer->m_pPed->m_fHealth = pVehicleDriverUpdate->playerHealth.iHealth;
    pNetworkPlayer->m_pPed->m_fArmour = pVehicleDriverUpdate->playerHealth.iArmour;
    pNetworkPlayer->m_oldControllerState = pVehicleDriverUpdate->playerKeys.oldControllerState;
    pNetworkPlayer->m_newControllerState = pVehicleDriverUpdate->playerKeys.newControllerState;

    pVehicle->m_nPrimaryColor = pVehicleDriverUpdate->color1;
    pVehicle->m_nSecondaryColor = pVehicleDriverUpdate->color2;

    pVehicle->m_fHealth = pVehicleDriverUpdate->health;

    if (pVehicle->GetRemapIndex() != pVehicleDriverUpdate->paintjob)
        pVehicle->SetRemap(pVehicleDriverUpdate->paintjob);

    if (pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
    {
        CAutomobile* pAutomobile = (CAutomobile*)pVehicle;
        pAutomobile->m_wMiscComponentAngle = pVehicleDriverUpdate->miscComponentAngle;
    }

    if (pVehicle->m_nVehicleType == VEHICLE_BIKE)
    {
        CBike* pBike = (CBike*)pVehicle;
        pBike->m_rideAnimData.m_fDesiredLeanAngle = pVehicleDriverUpdate->bikeLean;
    }

    if (pVehicle->m_nVehicleSubType == VEHICLE_PLANE)
    {
        pNetworkVehicle->m_bHasGear = true;
        pNetworkVehicle->m_bGearUp = pVehicleDriverUpdate->planeGearState > 0.0f;
    }

    pNetworkVehicle->m_pVehicle->m_eDoorLock = pVehicleDriverUpdate->locked;
    pNetworkVehicle->ApplyTrailer(pVehicleDriverUpdate->trailerid);

    // a passenger hears the driver's station
    CPlayerPed* pLocalPlayer = FindPlayerPed(0);
    if (pLocalPlayer && pLocalPlayer->m_pVehicle == pNetworkVehicle->m_pVehicle && pLocalPlayer->m_nPedFlags.bInVehicle &&
        pVehicleDriverUpdate->radioStation >= 0 && !AudioEngine.IsRadioRetuneInProgress() &&
        AudioEngine.GetCurrentRadioStationID() != pVehicleDriverUpdate->radioStation)
    {
        AudioEngine.RetuneRadio(pVehicleDriverUpdate->radioStation);
    }
    pNetworkVehicle->m_playerDriverSnapshot = *pVehicleDriverUpdate;
}

PACKET_HANDLER(ePacketType::VEHICLE_ENTER, Packets::Vehicles::VehicleEnter* pVehicleEnter)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pVehicleEnter->senderPlayerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleEnter->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    CPlayerPed* pPlayerPed = pNetworkPlayer->m_pPed;
    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;

    if (!pPlayerPed->IsVTableValid() || !pVehicle->IsVTableValid())
        return;

#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("player %s entered vehicleid %s %s", to_string(pVehicleEnter->senderPlayerid.value).c_str(),
        to_string(pVehicleEnter->vehicleid).c_str(), pVehicleEnter->seatid != 0 ? "as passenger" : "");
#endif

    // drop the jetpack if present
    if (auto pTask = pPlayerPed->m_pIntelligence->GetTaskJetPack())
    {
        pTask->MakeAbortable(pPlayerPed, ABORT_PRIORITY_URGENT, nullptr);
        pTask->m_nThrustStop = 0;
        pTask->m_nThrustFwd = 0;
        pTask->m_fThrustStrafe = 0.0f;
    }

    pNetworkPlayer->OnEnterVehicle(pVehicleEnter->vehicleid);

    if (pVehicleEnter->bPassenger)
    {
        pNetworkPlayer->EnterVehiclePassenger(pVehicle, pVehicleEnter->seatid);
    }
    else
    {
        if (pVehicleEnter->bForce)
        {
            pNetworkPlayer->WarpIntoVehicleDriver(pVehicle);
        }
        else
        {
            pPlayerPed->m_nPedFlags.CantBeKnockedOffBike = 1;  // 1 - never
            auto pTask = new CTaskComplexEnterCarAsDriver(pVehicle);
            pPlayerPed->m_pIntelligence->m_TaskMgr.SetTask(pTask, TASK_PRIMARY_PRIMARY, false);
        }
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_EXIT, Packets::Vehicles::VehicleExit* pVehicleExit)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("player %s exited from vehicle, reason %d", to_string(pVehicleExit->senderPlayerid.value).c_str(),
        pVehicleExit->reason);
#endif

    CVehicleExitSync::HandlePacket(*pVehicleExit);
}

PACKET_HANDLER(ePacketType::VEHICLE_DAMAGE, Packets::Vehicles::VehicleDamage* pVehicleDamage)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("VehicleDamage__Handle");
#endif

    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleDamage->vehicleid))
    {
        pNetworkVehicle->OnSyncerPacket();
        if (auto pVehicle = pNetworkVehicle->m_pVehicle)
        {
            if (pVehicle->IsVTableValid())
            {
                pNetworkVehicle->ApplyDamage(*pVehicleDamage);
            }
        }
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_COMPONENT_ADD, Packets::Vehicles::VehicleComponentAdd* pVehicleComponentAdd)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleComponentAdd->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    if (!pVehicle || !pVehicle->IsVTableValid())
    {
        return;
    }

    // the streaming runs on this thread: waiting with Sleep never let the paired parts load, the upgrade was dropped
    CStreaming::RequestModel(pVehicleComponentAdd->componentid, eStreamingFlags::GAME_REQUIRED);
    CStreaming::RequestVehicleUpgrade(pVehicleComponentAdd->componentid, eStreamingFlags::GAME_REQUIRED);
    CStreaming::LoadAllRequestedModels(false);
    if (!CStreaming::HasVehicleUpgradeLoaded(pVehicleComponentAdd->componentid))
    {
        logger::warn("vehicle %s: upgrade %d not loaded", to_string(pVehicleComponentAdd->vehicleid).c_str(),
            pVehicleComponentAdd->componentid);
        return;
    }

    pVehicle->AddVehicleUpgrade(pVehicleComponentAdd->componentid);
}

PACKET_HANDLER(
    ePacketType::VEHICLE_COMPONENT_REMOVE, Packets::Vehicles::VehicleComponentRemove* pVehicleComponentRemove)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicleComponentRemove->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    if (!pVehicle || !pVehicle->IsVTableValid())
    {
        return;
    }

    pVehicle->RemoveVehicleUpgrade(pVehicleComponentRemove->componentid);
}

PACKET_HANDLER(
    ePacketType::VEHICLE_PASSENGER_UPDATE, Packets::Vehicles::VehiclePassengerUpdate* pVehiclePassengerUpdate)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehiclePassengerUpdate->vehicleid);
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pVehiclePassengerUpdate->senderPlayerid);

    if (pNetworkVehicle == nullptr || pNetworkPlayer == nullptr)
    {
        return;
    }

    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    CPlayerPed* pPlayerPed = pNetworkPlayer->m_pPed;

    if (pVehicle == nullptr || pPlayerPed == nullptr)
    {
        return;
    }

    if (!pVehicle->IsVTableValid() || !pPlayerPed->IsVTableValid())
    {
        return;
    }

    // sent before the sender got out; or the sender is still being dragged out while our copy of the jacker is done
    if (pNetworkPlayer->HasJustLeftVehicle(pVehiclePassengerUpdate->vehicleid) ||
        (pNetworkPlayer->IsJackAllowed() && !pPlayerPed->m_nPedFlags.bInVehicle))
    {
        return;
    }

    if (!pPlayerPed->m_nPedFlags.bInVehicle || pPlayerPed->m_pVehicle != pVehicle ||
        pNetworkVehicle->m_pVehicle->m_apPassengers[pVehiclePassengerUpdate->seatid] != pNetworkPlayer->m_pPed)
    {
        // our enter task may lag behind the sender's, a warp would cut its get in anim; the next update comes soon
        if (pNetworkPlayer->IsEnteringVehicle(pVehicle))
        {
            return;
        }

#ifdef PACKET_DEBUG_MESSAGES
        CChat::AddMessage("forcing enter passenger %s", to_string(pNetworkPlayer->m_playerId).c_str());
#endif

        pNetworkPlayer->WarpIntoVehiclePassenger(pNetworkVehicle->m_pVehicle, pVehiclePassengerUpdate->seatid);
    }

    // the sender sits: an on foot update from now on is a real exit (a jacking sends no VEHICLE_EXIT)
    pNetworkPlayer->m_nEnterVehicleTime = 0;

    pNetworkPlayer->ApplyWeaponSnapshot(pVehiclePassengerUpdate->playerWeapon);
    pNetworkPlayer->m_onFootSnapshotInterpolated.healthSnapshot = pVehiclePassengerUpdate->playerHealth;
    pNetworkPlayer->m_onFootSnapshotInterpolated.iBreath = 100;  // breath is only sent on foot
    pNetworkPlayer->m_pPed->m_fHealth = pVehiclePassengerUpdate->playerHealth.iHealth;
    pNetworkPlayer->m_pPed->m_fArmour = pVehiclePassengerUpdate->playerHealth.iArmour;
    pNetworkPlayer->m_newControllerState = pVehiclePassengerUpdate->playerKeys.newControllerState;
    pNetworkPlayer->m_oldControllerState = pVehiclePassengerUpdate->playerKeys.oldControllerState;

    if (pVehiclePassengerUpdate->driveby && !CDriveBy::IsPedInDriveby(pPlayerPed))
    {
        CDriveBy::StartDriveby(pPlayerPed);
    }
    else if (!pVehiclePassengerUpdate->driveby && CDriveBy::IsPedInDriveby(pPlayerPed))
    {
        CDriveBy::StopDriveby(pPlayerPed);
    }
}

PACKET_HANDLER(ePacketType::ASSIGN_VEHICLE, Packets::Vehicles::AssignVehicleSyncer* pAssignVehicleSyncer)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pAssignVehicleSyncer->vehicleid);

    if (!pNetworkVehicle || pNetworkVehicle->m_bSyncing == pAssignVehicleSyncer->isSyncer)
    {
        return;
    }

#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("%s VEHICLE %s", pAssignVehicleSyncer->isSyncer ? "SYNCING" : "NOT SYNCING",
        to_string(pAssignVehicleSyncer->vehicleid).c_str());
#endif

    // a player drives it or rides in it, no ai to resume or stop
    if (pAssignVehicleSyncer->isSyncer)
    {
        pNetworkVehicle->BecomeSyncer(nullptr);
    }
    else
    {
        pNetworkVehicle->m_bSyncing = false;
        pNetworkVehicle->OnSyncerPacket();
        if (pNetworkVehicle->m_bTrain)
        {
            CTrainSync::OnBecomeClone(pNetworkVehicle);
        }
        else if (auto pVehicle = pNetworkVehicle->m_pVehicle)
        {
            pVehicle->SetVehicleCreatedBy(eVehicleCreatedBy::MISSION_VEHICLE);
        }
    }
}

PACKET_HANDLER(ePacketType::VEHICLE_REQUEST_OWNERSHIP, Packets::Vehicles::VehicleRequestOwnership* pRequest)
{
    // someone asked the server for our vehicle
    Packets::Vehicles::VehicleHandoff handoff{};
    handoff.vehicleid = pRequest->vehicleid;

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pRequest->vehicleid);
    CPlayerPed* pLocalPlayer = FindPlayerPed(0);

    bool bLocalPlayerInside = pNetworkVehicle && pNetworkVehicle->m_pVehicle && pLocalPlayer &&
                              pLocalPlayer->m_nPedFlags.bInVehicle && pLocalPlayer->m_pVehicle == pNetworkVehicle->m_pVehicle;

    if (pNetworkVehicle && pNetworkVehicle->m_bSyncing && pNetworkVehicle->m_pVehicle && !bLocalPlayerInside &&
        pNetworkVehicle->m_nCreatedBy != MISSION_VEHICLE)
    {
        pNetworkVehicle->BecomeClone(&handoff);
        logger::info("vehicle %s released with %d npc peds", to_string(handoff.vehicleid).c_str(), handoff.occupantCount);
    }

    GetPacketFactory().Send(handoff);
}

PACKET_HANDLER(ePacketType::VEHICLE_HANDOFF, Packets::Vehicles::VehicleHandoff* pHandoff)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pHandoff->vehicleid);
    if (!pNetworkVehicle)
    {
        // the server takes us for its syncer now, nobody syncs it; DropStaleClones/the server clean it up
        logger::warn("handoff of %s that we don't have", to_string(pHandoff->vehicleid).c_str());
        return;
    }

    // peds first: the driver has to be ours before the vehicle gives it the driving task
    for (int i = 0; i < pHandoff->occupantCount; i++)
    {
        if (CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pHandoff->occupants[i].pedid))
        {
            pNetworkPed->BecomeSyncer(true);
        }
    }

    pNetworkVehicle->BecomeSyncer(pHandoff);
    CDebugOwnership::WatchVehicle(pHandoff->vehicleid);
    logger::info("vehicle %s taken over with %d npc peds, mission %d style %d speed %d",
        to_string(pHandoff->vehicleid).c_str(), pHandoff->occupantCount, pHandoff->carMission, pHandoff->drivingStyle,
        pHandoff->cruiseSpeed);
}


PACKET_HANDLER(ePacketType::SET_VEHICLE_CREATED_BY, Packets::Vehicles::SetVehicleCreatedBy* pSetVehicleCreatedBy)
{
    if (CLocalPlayer::m_bIsHost)
    {
        return;
    }

    if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pSetVehicleCreatedBy->vehicleid))
    {
        pNetworkVehicle->m_nCreatedBy = pSetVehicleCreatedBy->createdBy;

        if (pNetworkVehicle->m_bSyncing && !pNetworkVehicle->m_bTrain)
        {
            if (auto pVehicle = pNetworkVehicle->m_pVehicle)
            {
                pVehicle->SetVehicleCreatedBy(pSetVehicleCreatedBy->createdBy);
            }
        }
    }
}