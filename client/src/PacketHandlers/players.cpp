#include "network/packets/players.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "CPedAnimSync.h"
#include <CAimSync.h>
#include <CEntryExitTransitionSync.h>
#include <CWantedSync.h>
#include <CCheatSync.h>
#include <CProjectileInfo.h>

// between the ped entering or leaving a vehicle here and the sender's on foot position
static constexpr float ENTER_VEHICLE_GIVE_UP_DISTANCE = 5.0f;

PACKET_HANDLER(ePacketType::PLAYER_ONFOOT_UPDATE, Packets::Players::OnFootUpdate* pOnFootUpdate)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pOnFootUpdate->senderPlayerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    CPlayerPed* pPlayerPed = pNetworkPlayer->m_pPed;

    if (pPlayerPed == nullptr)
    {
        pNetworkPlayer->CreatePed(pOnFootUpdate->vecPos);
        pPlayerPed = pNetworkPlayer->m_pPed;
    }

    bool bLeaving = false;
    if (pPlayerPed->m_pVehicle != nullptr && pPlayerPed->m_nPedFlags.bInVehicle)
    {
        // our enter task got the ped in before the sender's did: its last on foot updates are still on the way
        if (pNetworkPlayer->HasJustEnteredVehicle())
        {
            return;
        }

        // our copy of the leave task started later than the sender's, it gets out on its own (the get out anim, the
        // door); the sender far away gave it up
        bLeaving = pNetworkPlayer->IsLeavingVehicle(pPlayerPed->m_pVehicle) &&
                   (pPlayerPed->GetPosition() - pOnFootUpdate->vecPos).Magnitude() <= ENTER_VEHICLE_GIVE_UP_DISTANCE;
        // the sender's exit that waited for it has the way it got out (a fall), else a plain warp out
        if (!bLeaving && !CVehicleExitSync::ApplyPendingExit(pNetworkPlayer))
        {
            logger::info("%s out of its vehicle by an on foot update", pNetworkPlayer->GetName().c_str());
            pNetworkPlayer->m_nLeaveVehicleTime = 0;
            pNetworkPlayer->RemoveFromVehicle(pPlayerPed->m_pVehicle);
        }
    }

    // on the way in the enter task moves the ped (the walk to the door, the get in anim), a position would fight it;
    // the sender far away gave the enter up
    CNetworkVehicle* pEnterVehicle = CNetworkVehicleManager::GetVehicle(pNetworkPlayer->m_enterVehicleId);
    bool bEntering = pEnterVehicle && pNetworkPlayer->IsEnteringVehicle(pEnterVehicle->m_pVehicle);
    if (bEntering && (pPlayerPed->GetPosition() - pOnFootUpdate->vecPos).Magnitude() > ENTER_VEHICLE_GIVE_UP_DISTANCE)
    {
        pPlayerPed->m_pIntelligence->FlushImmediately(false);
        pNetworkPlayer->m_nEnterVehicleTime = 0;
        bEntering = false;
    }

    if (!bEntering && !bLeaving)
    {
        pNetworkPlayer->m_pPed->SetPosn(pOnFootUpdate->vecPos);
        pNetworkPlayer->m_pPed->m_vecMoveSpeed = pOnFootUpdate->vecMoveSpeed;
    }

    pNetworkPlayer->ApplyWeaponSnapshot(pOnFootUpdate->weaponSnapshot);

    pNetworkPlayer->m_pPed->m_fCurrentRotation = pOnFootUpdate->currentRotation.m_angle;
    pNetworkPlayer->m_pPed->m_fAimingRotation = pOnFootUpdate->aimingRotation.m_angle;

    CUtil::SetPlayerJetpack(pNetworkPlayer, pOnFootUpdate->bHasJetpack);

    if (CUtil::IsDucked(pNetworkPlayer->m_pPed) != pOnFootUpdate->bDucking)
    {
        if (pOnFootUpdate->bDucking)
        {
            pNetworkPlayer->m_pPed->m_pIntelligence->SetTaskDuckSecondary(0);
            // abort fighting and jumping tasks maybe?
        }
        else
        {
            pNetworkPlayer->m_pPed->m_pIntelligence->ClearTaskDuckSecondary();
        }
    }

    pNetworkPlayer->m_pPed->m_nFightingStyle = pOnFootUpdate->iFightingStyle;
    pNetworkPlayer->m_pPed->m_nAllowedAttackMoves |= 15u;

    pNetworkPlayer->m_oldControllerState = pOnFootUpdate->keySnapshot.oldControllerState;
    pNetworkPlayer->m_newControllerState = pOnFootUpdate->keySnapshot.newControllerState;

    CPedAnimSync::Apply(pNetworkPlayer->m_pPed, pOnFootUpdate->idleAnims, pOnFootUpdate->idleAnimCount,
        ANIM_GROUP_PLAYIDLES);

    pNetworkPlayer->m_onFootSnapshotInterpolated = *pOnFootUpdate;
}

PACKET_HANDLER(ePacketType::PLAYER_KEY_SYNC, Packets::Players::KeyPressed* pKeyPressed)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pKeyPressed->senderPlayerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    pNetworkPlayer->m_oldControllerState = pKeyPressed->keySnapshot.oldControllerState;
    pNetworkPlayer->m_newControllerState = pKeyPressed->keySnapshot.newControllerState;

    pNetworkPlayer->m_pPed->m_fCurrentRotation = pKeyPressed->currentRotation.m_angle;
    pNetworkPlayer->m_pPed->m_fAimingRotation = pKeyPressed->aimingRotation.m_angle;

    pNetworkPlayer->m_onFootSnapshotInterpolated.currentRotation = pKeyPressed->currentRotation;
    pNetworkPlayer->m_onFootSnapshotInterpolated.aimingRotation = pKeyPressed->aimingRotation;
}

PACKET_HANDLER(ePacketType::PLAYER_CAMERA_SYNC, Packets::Players::PlayerCameraSync* pPlayerCameraSync)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPlayerCameraSync->senderPlayerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    pNetworkPlayer->m_startedInterpolatingCameraAt = GetTickCount();
    pNetworkPlayer->m_cameraSnapshotOld = pNetworkPlayer->m_cameraSnapshot;
    pNetworkPlayer->m_cameraSnapshot = *pPlayerCameraSync;
}

PACKET_HANDLER(ePacketType::SET_PLAYER_TASK, Packets::Players::SetPlayerTask* pSetPlayerTask)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pSetPlayerTask->senderPlayerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    pNetworkPlayer->HandleTask(*pSetPlayerTask);
}

PACKET_HANDLER(ePacketType::ENEX_TRANSITION, Packets::Players::EnExTransition* pEnExTransition)
{
    CEntryExitTransitionSync::Receive(*pEnExTransition);
}

PACKET_HANDLER(ePacketType::PLAYER_PLACE_WAYPOINT, Packets::Players::PlayerPlaceWaypoint* pPlayerPlaceWaypoint)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPlayerPlaceWaypoint->senderPlayerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    pNetworkPlayer->m_waypointState = *pPlayerPlaceWaypoint;

#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("WAYPOINT PLACE %d %.0f %.0f\n", packet->place, packet->position.x, packet->position.y);
#endif
}

PACKET_HANDLER(ePacketType::RESPAWN_PLAYER, Packets::Players::RespawnPlayer* pRespawnPlayer)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pRespawnPlayer->senderPlayerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    pNetworkPlayer->Respawn();
}

PACKET_HANDLER(ePacketType::PLAYER_BULLET_SHOT, Packets::Players::PlayerBulletShot* pPlayerBulletShot)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPlayerBulletShot->senderPlayerid);
    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    pNetworkPlayer->m_pPed->SetCurrentWeapon(pPlayerBulletShot->iWeaponType);

    CEntity* owner = pNetworkPlayer->m_pPed;
    CEntity* victim = nullptr;
    CVector startPoint = pPlayerBulletShot->startPos;
    CVector endPoint = pPlayerBulletShot->endPos;
    CColPoint colPoint;
    int incrementalHit = pPlayerBulletShot->incrementalHit;

    if (pPlayerBulletShot->bHitSomething)
    {
        CVector endPosCalced = (endPoint - startPoint);
        endPosCalced.Normalise();
        endPosCalced = endPoint + endPosCalced * 10.0f;
        endPoint = endPosCalced;
        CWorld::ProcessLineOfSight(startPoint, endPoint, colPoint, victim, 1, 1, 1, 1, 1, 0, 0, 1);
        colPoint.m_vecPoint = pPlayerBulletShot->endPos;

        if (pPlayerBulletShot->bHitNetworkEntity)
        {
            victim = pPlayerBulletShot->hitEntity.GetEntity();
            if (pPlayerBulletShot->hitEntity.entityType == NETWORK_ENTITY_TYPE_PLAYER ||
                pPlayerBulletShot->hitEntity.entityType == NETWORK_ENTITY_TYPE_PED)
            {
                colPoint.m_nSurfaceTypeB = pPlayerBulletShot->pedPiece;
            }
            else if (pPlayerBulletShot->hitEntity.entityType == NETWORK_ENTITY_TYPE_VEHICLE)
            {
                // our own ray may miss the wheel the shooter hit by a few centimeters
                colPoint.m_nPieceTypeB = pPlayerBulletShot->vehiclePiece;
            }
        }
    }
    else
    {
        colPoint.m_vecPoint.Set(0.0f, 0.0f, 0.0f);
        colPoint.m_vecNormal.Set(0.0f, 0.0f, 0.0f);
        colPoint.m_nLightingB.day = 0;
        colPoint.m_nLightingB.night = 0;
        colPoint.m_nPieceTypeB = 0;
        colPoint.m_nSurfaceTypeB = 0;
    }

    if (victim && !victim->IsVTableValid())
    {
        victim = nullptr;
    }

    pNetworkPlayer->m_pPed->GetWeapon().DoBulletImpact(
        owner, victim, &startPoint, &pPlayerBulletShot->endPos, &colPoint, incrementalHit);
}

PACKET_HANDLER(ePacketType::ADD_PROJECTILE, Packets::Players::AddProjectile* pAddProjectile)
{
    if (pAddProjectile->creator.GetEntity() == nullptr)
    {
        return;
    }

    CVector origin = pAddProjectile->origin;
    CVector dir = pAddProjectile->dir;

    if (pAddProjectile->creator.entityType == NETWORK_ENTITY_TYPE_PLAYER)
    {
        if (auto networkPlayer = CNetworkPlayerManager::GetPlayer(pAddProjectile->creator.GetEntity()))
        {
            CAimSync::ApplyNetworkPlayerContext(networkPlayer);
        }
    }

    CEntity* pTarget = nullptr;
    if (pAddProjectile->bTarget)
    {
        pTarget = pAddProjectile->target.GetEntity();
    }
    if (pAddProjectile->bDir)
    {
        CProjectileInfo::AddProjectile(pAddProjectile->creator.GetEntity(), pAddProjectile->projectileType, origin, pAddProjectile->force, nullptr, pTarget);
    }
    else
    {
        CProjectileInfo::AddProjectile(pAddProjectile->creator.GetEntity(), pAddProjectile->projectileType, origin, pAddProjectile->force, &pAddProjectile->dir, pTarget);
    }

    if (pAddProjectile->creator.entityType == NETWORK_ENTITY_TYPE_PLAYER && CNetworkPlayerManager::GetPlayer(pAddProjectile->creator.GetEntity()))
    {
        CAimSync::ApplyLocalContext();
    }
}

PACKET_HANDLER(ePacketType::PLAYER_STATS, Packets::Players::PlayerStats* pPlayerStats)
{
    if (auto pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPlayerStats->senderPlayerid))
    {
        for (size_t i = 0; i < CStatsSync::SYNCED_STATS_COUNT; i++)
        {
            eStats statId = CStatsSync::m_aeSyncedStats[i];
            pNetworkPlayer->m_stats[statId] = pPlayerStats->stats[i];
        }
    }
}

PACKET_HANDLER(ePacketType::REBUILD_PLAYER, Packets::Players::RebuildPlayer* pRebuildPlayer)
{
    if (auto pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pRebuildPlayer->senderPlayerid))
    {
        pNetworkPlayer->m_stats[STAT_FAT] = pRebuildPlayer->clothesDesc.m_fFatStat;
        pNetworkPlayer->m_stats[STAT_MUSCLE] = pRebuildPlayer->clothesDesc.m_fMuscleStat;

        //CStatsSync::ApplyNetworkPlayerContext(networkPlayer);

        pNetworkPlayer->m_pPedClothesDesc = pRebuildPlayer->clothesDesc;

        if (auto pPlayerPed = pNetworkPlayer->m_pPed)
        {
            *pPlayerPed->m_pPlayerData->m_pPedClothesDesc = pRebuildPlayer->clothesDesc;
            if (pPlayerPed->m_pRwClump)
            {
                CClothes::RebuildPlayer(pPlayerPed, false);
            }
        }

        //CStatsSync::ApplyLocalContext();
    }
}

PACKET_HANDLER(ePacketType::PLAYER_WANTED_LEVEL, Packets::Players::PlayerWantedLevel* pPlayerWantedLevel)
{
    CWantedSync::HandlePacket(pPlayerWantedLevel);
}

PACKET_HANDLER(ePacketType::PLAYER_FALL, Packets::Players::PlayerFall* pPlayerFall)
{
    CPlayerFallSync::HandlePacket(*pPlayerFall);
}

PACKET_HANDLER(ePacketType::PLAYER_CHEAT, Packets::Players::PlayerCheat* pPlayerCheat)
{
    CCheatSync::HandlePacket(pPlayerCheat);
}
