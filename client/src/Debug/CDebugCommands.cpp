#include "stdafx.h"
#include "CDebugCommands.h"
#include "CScreenLog.h"
#include <fstream>
#include <sstream>
#include <CWanted.h>
#include <CGangWars.h>
#include <CPickups.h>
#include "Hooks/PlayerHooks.h"
#include "Hooks/WorldHooks.h"
#include <CDummyObject.h>
#include <CTaskComplexEnterCarAsDriver.h>
#include <CCarEnterExit.h>
#include "CPassengerEnter.h"
#include <CFireManager.h>
#include <CAnimBlendHierarchy.h>
#include "CStandAgent.h"

static CTrain* FindNearestFrontCarriage(const CVector& position, bool bMissionOnly = false)
{
    CTrain* pNearest = nullptr;
    float fNearestDistance = 1000.0f;

    for (auto pVehicle : CPools::ms_pVehiclePool)
    {
        if (CUtil::GetVehicleType(pVehicle) != VEHICLE_TRAIN || !static_cast<CTrain*>(pVehicle)->m_nTrainFlags.bIsFrontCarriage)
        {
            continue;
        }

        if (bMissionOnly && !static_cast<CTrain*>(pVehicle)->m_nTrainFlags.bMissionTrain)
        {
            continue;
        }

        float fDistance = (pVehicle->GetPosition() - position).Magnitude();
        if (fDistance < fNearestDistance)
        {
            pNearest = static_cast<CTrain*>(pVehicle);
            fNearestDistance = fDistance;
        }
    }
    return pNearest;
}

// the map objects around, the nearest first
static std::vector<CObject*> FindMapObjects(const CVector& position, float radius)
{
    std::vector<std::pair<float, CObject*>> found;
    for (auto pObject : CPools::ms_pObjectPool)
    {
        if (pObject->m_nObjectType != 1 || !pObject->m_pDummyObject)  // OBJECT_GAME
        {
            continue;
        }

        float fDistance = (pObject->GetPosition() - position).Magnitude();
        if (fDistance < radius)
        {
            found.push_back({fDistance, pObject});
        }
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<CObject*> objects;
    for (const auto& [distance, pObject] : found)
    {
        objects.push_back(pObject);
    }
    return objects;
}

static void LogMapObjects(const CVector& position)
{
    for (CObject* pObject : FindMapObjects(position, 40.0f))
    {
        const CVector& pos = pObject->GetPosition();
        const CVector& original = pObject->m_pDummyObject->GetPosition();
        logger::info("stand debug: object %d at %.2f %.2f %.2f (dummy %.2f %.2f %.2f) effect %d damaged %d collision %d "
                     "visible %d broken %d exploded %d static %d up.z %.2f explodes %d",
            pObject->m_nModelIndex, pos.x, pos.y, pos.z, original.x, original.y, original.z,
            pObject->m_nColDamageEffect, pObject->m_bRenderDamaged, pObject->m_bUsesCollision, pObject->m_bIsVisible,
            pObject->m_nObjectFlags.bIsBroken, pObject->m_nObjectFlags.bIsExploded, pObject->m_bIsStatic,
            pObject->m_matrix ? pObject->m_matrix->at.z : 1.0f,
            pObject->m_pObjectInfo ? pObject->m_pObjectInfo->m_bCausesExplosion : 0);
    }
}

static void CreateMissionTrain(int type, CPlayerPed* pPlayer)
{
    if (type < 0 || type >= TrainState::TRAIN_TYPE_COUNT)
    {
        return;
    }

    // a script loads the models before the command
    for (int* pModel = reinterpret_cast<int*>(0x8D44F8) + type * 16; *pModel; pModel++)
    {
        CStreaming::RequestModel(*pModel, GAME_REQUIRED);
    }
    CStreaming::LoadAllRequestedModels(false);

    const CVector& pos = pPlayer->GetPosition();
    int handle = 0;
    plugin::Command<Commands::CREATE_MISSION_TRAIN>(type, pos.x, pos.y, pos.z, true, &handle);
    logger::info("stand debug: mission train of type %d created, handle %d", type, handle);
}

// headshots from the player at the members of the gang of the current war (its attackers, also the clones of someone
// else's war) closer than 100 m, through the synced shot path
static void ShootGangMembers(CPlayerPed* pPlayer, int count)
{
    pPlayer->GiveWeapon(WEAPON_M4, 500, false);
    pPlayer->SetCurrentWeapon(WEAPON_M4);
    CWeapon& weapon = pPlayer->GetWeapon();

    int shot = 0;
    for (int i = 0; i < CPools::ms_pPedPool->m_nSize && shot < count; i++)
    {
        CPed* pPed = CPools::ms_pPedPool->GetAt(i);
        if (!pPed || pPed->IsPlayer() || pPed->m_nPedType != CGangWars::Gang1 + PED_TYPE_GANG1 ||
            pPed->m_fHealth <= 0.0f || (pPed->GetPosition() - pPlayer->GetPosition()).Magnitude() > 100.0f)
        {
            continue;
        }

        for (int bullet = 0; bullet < 6 && pPed->m_fHealth > 0.0f; bullet++)
        {
            CVector start = pPlayer->GetPosition() + CVector(0.0f, 0.0f, 0.7f);
            CVector end = pPed->GetPosition() + CVector(0.0f, 0.0f, 0.6f);
            CColPoint colPoint{};
            colPoint.m_vecPoint = end;
            colPoint.m_vecNormal = start - end;
            colPoint.m_vecNormal.Normalise();
            colPoint.m_nSurfaceTypeB = 9; // PED_PIECE_HEAD, CollectShotSnapshot takes it from here
            colPoint.m_nPieceTypeB = 9;
            PlayerHooks::DoLocalPlayerBulletImpact(&weapon, pPed, &start, &end, &colPoint);
        }
        shot++;
    }
    logger::info("stand debug: shot %d members of gang %d", shot, CGangWars::Gang1);
}

static CVehicle* FindNearestVehicle(const CVector& position, float fMaxDistance)
{
    CVehicle* pNearest = nullptr;
    for (auto pVehicle : CPools::ms_pVehiclePool)
    {
        float fDistance = (pVehicle->GetPosition() - position).Magnitude();
        if (fDistance < fMaxDistance)
        {
            pNearest = pVehicle;
            fMaxDistance = fDistance;
        }
    }
    return pNearest;
}

static std::string ms_watchedPed;  // to_string of a ped id watch-anims logs too (watch-ped, ram-ped)
static int ms_nLastCar = -1;  // the script handle of the last `car`
static std::string ms_watchedVehicle;  // to_string of a vehicle id blow, vehicle-health, vehicle-state act on

// what a player ped plays and does, a line on every change: the active task, in a vehicle, the car anims
static void LogPlayerAnims()
{
    static std::unordered_map<CPed*, std::string> lastState;

    auto log = [](CPed* pPed, const char* name)
    {
        if (pPed == nullptr || pPed->m_pRwClump == nullptr)
        {
            return;
        }

        CTask* pTask = pPed->m_pIntelligence->m_TaskMgr.GetSimplestActiveTask();
        int nSlot = 0;  // the primary slot of the active task: 0 physical response, 1-2 event responses, 3 primary
        while (nSlot < 5 && !pPed->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[nSlot])
        {
            nSlot++;
        }
        char szState[256];
        // the weapon in the hand: its type, its model and whether the model is in the hand (m_pWeaponObject)
        int n = sprintf_s(szState, "fire %d slot %d task %d in %d seat %d weapon %d model %d obj %d anims",
            pPed->m_pFire != nullptr, nSlot, pTask ? pTask->GetTaskType() : -1, pPed->m_nPedFlags.bInVehicle,
            pPed->m_pVehicle && pPed->m_pVehicle->m_pDriver == pPed ? 0 : 1, pPed->GetWeapon().m_eWeaponType,
            pPed->m_nWeaponModelId, pPed->m_pWeaponObject != nullptr);
        for (auto pAssoc = RpAnimBlendClumpGetFirstAssociation(pPed->m_pRwClump); pAssoc && n < 240;
             pAssoc = RpAnimBlendGetNextAssociation(pAssoc))
        {
            if (pAssoc->m_fBlendAmount > 0.1f)
            {
                n += sprintf_s(szState + n, sizeof(szState) - n, " %d", pAssoc->m_nAnimId);
                // a named anim of a script: which one and its second, to compare the phase with the host's
                if (pAssoc->m_nAnimId == -1 && pAssoc->m_pHierarchy)
                {
                    n += sprintf_s(szState + n, sizeof(szState) - n, "(%04x@%d)",
                        pAssoc->m_pHierarchy->m_hashKey & 0xFFFF, static_cast<int>(pAssoc->m_fCurrentTime));
                }
            }
        }

        if (lastState[pPed] != szState)
        {
            lastState[pPed] = szState;
            logger::info("stand debug: anims %s %s", name, szState);
        }
    };

    log(FindPlayerPed(0), "local");


    // the doors of the nearest car: a clone's door sync must not fight the enter / exit tasks
    static std::string lastDoors;
    CPlayerPed* pPlayer = FindPlayerPed(0);
    CVehicle* pVehicle = pPlayer ? FindNearestVehicle(pPlayer->GetPosition(), 10.0f) : nullptr;
    if (pVehicle && pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
    {
        auto pAutomobile = static_cast<CAutomobile*>(pVehicle);
        CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle);
        // who drives: 0 nobody, 1 we, 2 a remote player, 3 a ped
        CPed* pDriver = pVehicle->m_pDriver;
        int nDriver = !pDriver ? 0 : pDriver == pPlayer ? 1 : CNetworkPlayerManager::GetPlayer(pDriver) ? 2 : 3;
        char szDoors[160];
        sprintf_s(szDoors, "driver %d clone %d lf %.1f rf %.1f lr %.1f rr %.1f in %x out %x", nDriver,
            pNetworkVehicle && !pNetworkVehicle->m_bSyncing, pAutomobile->m_doors[DOOR_FRONT_LEFT].GetAngleOpenRatio(),
            pAutomobile->m_doors[DOOR_FRONT_RIGHT].GetAngleOpenRatio(), pAutomobile->m_doors[DOOR_REAR_LEFT].GetAngleOpenRatio(),
            pAutomobile->m_doors[DOOR_REAR_RIGHT].GetAngleOpenRatio(), pVehicle->m_nGettingInFlags, pVehicle->m_nGettingOutFlags);
        if (lastDoors != szDoors)
        {
            lastDoors = szDoors;
            logger::info("stand debug: doors %u %s", CTimer::m_snTimeInMilliseconds, szDoors);
        }
    }
    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        log(pNetworkPlayer->m_pPed, pNetworkPlayer->GetName().c_str());
    }

    // a ped clone playing the enter task of its syncer (PED_VEHICLE_ENTER), a jacker; the watched ped
    for (auto pNetworkPed : CNetworkPedManager::m_pPeds)
    {
        // the watched ped; a ped in a stealth kill or dying
        CPed* pWatched = pNetworkPed->m_pPed;
        bool bInteresting = pWatched && (pWatched->m_ePedState == PEDSTATE_DIE_BY_STEALTH ||
                                         pWatched->m_ePedState == PEDSTATE_DIE || pWatched->m_ePedState == PEDSTATE_DEAD);
        if (bInteresting || (!ms_watchedPed.empty() && to_string(pNetworkPed->m_pedId) == ms_watchedPed))
        {
            char szName[64];
            sprintf_s(szName, "watched %s (ours %d)", to_string(pNetworkPed->m_pedId).c_str(), pNetworkPed->m_bSyncing);
            log(pNetworkPed->m_pPed, szName);
            continue;
        }

        // a passenger: the weapon in its hand in a car scene (SWEET4's homies)
        if (pWatched && pWatched->m_nPedFlags.bInVehicle && pWatched->m_pVehicle && pWatched->m_pVehicle->m_pDriver != pWatched)
        {
            char szName[64];
            sprintf_s(szName, "passenger %s (ours %d)", to_string(pNetworkPed->m_pedId).c_str(), pNetworkPed->m_bSyncing);
            log(pWatched, szName);
            continue;
        }

        if (!pNetworkPed->m_bSyncing && pNetworkPed->m_nEnterVehicleTime != 0 &&
            CTimer::m_snTimeInMilliseconds - pNetworkPed->m_nEnterVehicleTime < 15000)
        {
            CNetworkVehicle* pEnterVehicle = CNetworkVehicleManager::GetVehicle(pNetworkPed->m_enterVehicleId);
            char szName[64];
            sprintf_s(szName, "ped %s (lock %d)", to_string(pNetworkPed->m_pedId).c_str(),
                pEnterVehicle && pEnterVehicle->m_pVehicle ? pEnterVehicle->m_pVehicle->m_eDoorLock : -1);
            log(pNetworkPed->m_pPed, szName);
        }
    }
}

void CDebugCommands::RunCommand(const std::string& line)
{
    CPlayerPed* pPlayer = FindPlayerPed(0);
    if (!pPlayer)
    {
        return;
    }

    std::istringstream stream(line);
    std::string command;
    stream >> command;

    if (command == "police")
    {
        std::string state;
        stream >> state;
        ms_bPoliceOff = state == "off";
        if (!ms_bPoliceOff)
        {
            CWanted::SetMaximumWantedLevel(6);
        }
    }
    else if (command == "pickup-list")
    {
        // the slots of CPickups::aPickUps in a radius: index, ref, type, model, position, flags, regeneration - now
        float radius = 50.0f;
        stream >> radius;
        logger::info("stand debug: pickups in %.0f m, money %d, horseshoes %.0f oysters %.0f snapshots %.0f", radius,
            CWorld::Players[0].m_nMoney, CStats::GetStatValue(STAT_HORSESHOES_COLLECTED),
            CStats::GetStatValue(STAT_OYSTERS_COLLECTED), CStats::GetStatValue(STAT_SNAPSHOTS_TAKEN));
        for (int i = 0; i < 620; i++)
        {
            CPickup& pickup = CPickups::aPickUps[i];
            if (pickup.m_nPickupType == PICKUP_NONE ||
                (pickup.GetPosn() - pPlayer->GetPosition()).Magnitude() > radius)
            {
                continue;
            }
            CVector pos = pickup.GetPosn();
            logger::info("stand debug: pickup %d ref %d type %d model %d at %.2f %.2f %.2f flags %02x ammo %u regen %d", i,
                pickup.m_nReferenceIndex, pickup.m_nPickupType, pickup.m_nModelIndex, pos.x, pos.y, pos.z,
                *reinterpret_cast<uint8_t*>(&pickup.m_nFlags), pickup.m_nAmmo,
                static_cast<int>(pickup.m_nRegenerationTime - CTimer::m_snTimeInMilliseconds));
        }
    }
    else if (command == "pickup-tp")
    {
        // onto the n-th nearest pickup every game has of its own (on street, once), or onto x y z
        std::string first;
        stream >> first;
        CVector target{};
        bool bFound = false;
        float y = 0.0f, z = 0.0f;
        if (stream >> y >> z)
        {
            target = CVector(std::stof(first), y, z);
            bFound = true;
        }
        else
        {
            // or the nearest collectible
            int model = first == "horseshoe" ? 954 : first == "oyster" ? 953 : first == "snapshot" ? 1253 : 0;
            int n = model || first.empty() ? 0 : std::stoi(first);
            std::vector<std::pair<float, int>> nearest;
            for (int i = 0; i < 620; i++)
            {
                CPickup& pickup = CPickups::aPickUps[i];
                bool bWanted = model ? pickup.m_nPickupType != PICKUP_NONE && pickup.m_nModelIndex == model
                                     : pickup.m_nPickupType == PICKUP_ON_STREET ||
                                           pickup.m_nPickupType == PICKUP_ON_STREET_SLOW ||
                                           pickup.m_nPickupType == PICKUP_ONCE;
                if (bWanted)
                {
                    nearest.emplace_back((pickup.GetPosn() - pPlayer->GetPosition()).Magnitude(), i);
                }
            }
            std::sort(nearest.begin(), nearest.end());
            if (n < static_cast<int>(nearest.size()))
            {
                CPickup& pickup = CPickups::aPickUps[nearest[n].second];
                target = pickup.GetPosn();
                bFound = true;
                logger::info("stand debug: pickup %d type %d model %d, %d synced ones", nearest[n].second,
                    pickup.m_nPickupType, pickup.m_nModelIndex, static_cast<int>(nearest.size()));
            }
        }
        if (bFound)
        {
            pPlayer->Teleport(target + CVector(0.0f, 0.0f, 0.5f), false);
            logger::info("stand debug: on pickup at %.2f %.2f %.2f", target.x, target.y, target.z);
        }
    }
    else if (command == "mission")
    {
        // as the Missions window (MissionRunner): the host only, outside, no mission running
        int id = -1;
        stream >> id;
        if (!CLocalPlayer::m_bIsHost || CTheScripts::IsPlayerOnAMission() || CTheScripts::bAlreadyRunningAMissionScript ||
            id < 0 || id >= CTheScripts::NumberOfMissionScripts)
        {
            logger::info("stand debug: can't launch mission %d (host %d, on a mission %d)", id, CLocalPlayer::m_bIsHost,
                CTheScripts::IsPlayerOnAMission());
        }
        else
        {
            plugin::Command<Commands::LOAD_AND_LAUNCH_MISSION_INTERNAL>(id);
            logger::info("stand debug: mission %d launched", id);
        }
    }
    else if (command == "mission-state")
    {
        // on a mission, the player's position, health, the money, the wanted level
        CVector pos = pPlayer->GetPosition();
        logger::info("stand debug: on a mission %d at %.1f %.1f %.1f in vehicle %d health %.0f money %d",
            CTheScripts::IsPlayerOnAMission(), pos.x, pos.y, pos.z, pPlayer->m_nPedFlags.bInVehicle, pPlayer->m_fHealth,
            CWorld::Players[0].m_nMoney);
    }
    else if (command == "pickup-script")
    {
        // as a mission does it, through the opcodes: create <model> <type> (CREATE_PICKUP), collected
        // (HAS_PICKUP_BEEN_COLLECTED), remove (REMOVE_PICKUP); the last one made
        static int handle = -1;
        std::string action;
        stream >> action;
        if (action == "create")
        {
            int model = 1240, type = PICKUP_ONCE;
            stream >> model >> type;
            CVector pos = pPlayer->GetPosition() + pPlayer->GetForward() * 3.0f;
            plugin::Command<Commands::REQUEST_MODEL>(model);
            plugin::Command<Commands::LOAD_ALL_MODELS_NOW>();
            plugin::Command<Commands::CREATE_PICKUP>(model, type, pos.x, pos.y, pos.z, &handle);
            logger::info("stand debug: script pickup %d at %.2f %.2f %.2f", handle, pos.x, pos.y, pos.z);
        }
        else if (action == "collected")
        {
            bool bCollected = plugin::Command<Commands::HAS_PICKUP_BEEN_COLLECTED>(handle);
            logger::info("stand debug: script pickup %d collected %d", handle, bCollected);
        }
        else if (action == "remove")
        {
            plugin::Command<Commands::REMOVE_PICKUP>(handle);
            logger::info("stand debug: script pickup %d removed", handle);
        }
    }
    else if (command == "pickup-money" || command == "pickup-create")
    {
        // in front of the player: money as from a body (CreateSomeMoney), or any pickup as a script makes it
        CVector pos = pPlayer->GetPosition() + pPlayer->GetForward() * 2.0f;
        if (command == "pickup-money")
        {
            int amount = 100;
            stream >> amount;
            CPickups::CreateSomeMoney(pos, amount);
        }
        else
        {
            int model = 1240;  // health
            int type = PICKUP_ON_STREET;
            int ammo = 0;
            stream >> model >> type >> ammo;
            plugin::Command<Commands::REQUEST_MODEL>(model);
            plugin::Command<Commands::LOAD_ALL_MODELS_NOW>();
            CPickups::GenerateNewOne(pos, model, static_cast<unsigned char>(type), ammo, 0, false, nullptr);
        }
        logger::info("stand debug: pickup at %.2f %.2f %.2f", pos.x, pos.y, pos.z);
    }
    else if (command == "wanted")
    {
        int level = 4;
        stream >> level;
        plugin::Command<Commands::ALTER_WANTED_LEVEL>(0, level);
    }
    else if (command == "helis")
    {
        // police and news helis around: ours (we generated or got them) and clones
        int ours = 0, clones = 0;
        for (CNetworkVehicle* pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
        {
            CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
            if (pVehicle && (pVehicle->m_nModelIndex == 497 || pVehicle->m_nModelIndex == 488))
            {
                (pNetworkVehicle->m_bSyncing ? ours : clones)++;
            }
        }
        logger::info("stand debug: helis ours %d clones %d", ours, clones);
    }
    else if (command == "mission-train")
    {
        int type = 0;
        stream >> type;
        CreateMissionTrain(type, pPlayer);
    }
    else if (command == "delete-mission-trains")
    {
        plugin::Command<Commands::DELETE_MISSION_TRAINS>();
    }
    else if (command == "release-mission-trains")
    {
        plugin::Command<Commands::MARK_MISSION_TRAINS_AS_NO_LONGER_NEEDED>();
    }
    else if (command == "delete-all-trains")
    {
        plugin::Command<Commands::DELETE_ALL_TRAINS>();
    }
    else if (command == "drive-train")
    {
        if (CTrain* pTrain = FindNearestFrontCarriage(pPlayer->GetPosition()))
        {
            plugin::Command<Commands::WARP_CHAR_INTO_CAR>(CPools::GetPedRef(pPlayer), CPools::GetVehicleRef(pTrain));
        }
    }
    else if (command == "train-speed")
    {
        float speed = 0.0f;
        stream >> speed;
        if (pPlayer->m_pVehicle && CUtil::GetVehicleType(pPlayer->m_pVehicle) == VEHICLE_TRAIN)
        {
            CTrain::SetTrainSpeed(static_cast<CTrain*>(pPlayer->m_pVehicle), speed);
        }
    }
    else if (command == "teleport")
    {
        CVector pos{};
        stream >> pos.x >> pos.y >> pos.z;
        // the scene first, a writing of the matrix dropped the player through the not yet loaded collision
        CStreaming::LoadScene(&pos);
        if (pPlayer->m_pVehicle)
        {
            plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(CPools::GetPedRef(pPlayer), pos.x, pos.y, pos.z);
        }
        else
        {
            plugin::Command<Commands::SET_CHAR_COORDINATES>(CPools::GetPedRef(pPlayer), pos.x, pos.y, pos.z);
        }
    }
    else if (command == "teleport-car")
    {
        // the vehicle we drive with everyone inside (SET_CAR_COORDINATES), a mission's drive in a moment
        CVector pos{};
        float heading = 0.0f;
        stream >> pos.x >> pos.y >> pos.z >> heading;
        if (pPlayer->m_nPedFlags.bInVehicle && pPlayer->m_pVehicle)
        {
            CStreaming::LoadScene(&pos);
            int car = CPools::GetVehicleRef(pPlayer->m_pVehicle);
            plugin::Command<Commands::SET_CAR_COORDINATES>(car, pos.x, pos.y, pos.z);
            plugin::Command<Commands::SET_CAR_HEADING>(car, heading);
        }
    }
    else if (command == "gang-wars")
    {
        std::string state;
        stream >> state;
        plugin::Call<0x446570, bool>(state != "off"); // CGangWars::SetGangWarsActive
    }
    else if (command == "forget-vehicle")
    {
        // the leak of a vehicle flood: our nearest vehicle goes away here without VEHICLE_REMOVE, the others keep
        // the clone (CNetworkVehicleManager::DropStaleClones must take it)
        CNetworkVehicle* pNearest = nullptr;
        float fNearest = 200.0f;
        for (auto pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
        {
            if (pNetworkVehicle->m_bSyncing && !pNetworkVehicle->m_bTrain && pNetworkVehicle->m_pVehicle &&
                pNetworkVehicle->m_pVehicle != pPlayer->m_pVehicle)
            {
                float fDistance = (pNetworkVehicle->m_pVehicle->GetPosition() - pPlayer->GetPosition()).Magnitude();
                if (fDistance < fNearest)
                {
                    pNearest = pNetworkVehicle;
                    fNearest = fDistance;
                }
            }
        }
        if (pNearest)
        {
            logger::info("stand debug: forgetting %s of model %d", to_string(pNearest->m_vehicleId).c_str(),
                pNearest->m_nModelId);
            pNearest->m_bSyncing = false; // the destructor deletes it as a clone, without a packet
            CNetworkVehicleManager::Remove(pNearest);
            delete pNearest;
        }
    }
    else if (command == "flash-vehicle")
    {
        // the vehicle flood: a car generator that creates a vehicle and removes it right away; a VEHICLE_REMOVE
        // relayed with our serverTime came before the server's VEHICLE_SPAWN at the others, the clone stayed there
        int count = 10;
        stream >> count;

        constexpr int MODEL = 400; // landstal
        CStreaming::RequestModel(MODEL, 0);
        CStreaming::LoadAllRequestedModels(false);

        int flashed = 0;
        for (int i = 0; i < count; i++)
        {
            CVehicle* pVehicle = new CAutomobile(MODEL, RANDOM_VEHICLE, true);
            if (!pVehicle)
            {
                break;
            }
            pVehicle->SetPosn(pPlayer->GetPosition() + CVector(5.0f + 5.0f * (i % 4), 5.0f * (i / 4 % 4), 0.0f));
            CNetworkVehicle::CreateHosted(pVehicle);
            CWorld::Add(pVehicle);

            WorldHooks::CWorld__Remove_Hook(pVehicle);
            delete pVehicle;
            flashed++;
        }
        logger::info("stand debug: created and removed %d vehicles", flashed);
    }
    else if (command == "god")
    {
        // the attack waves of a gang war kill a tester standing still
        pPlayer->m_nPhysicalFlags.bBulletProof = true;
        pPlayer->m_nPhysicalFlags.bFireProof = true;
        pPlayer->m_nPhysicalFlags.bExplosionProof = true;
        pPlayer->m_nPhysicalFlags.bMeleeProof = true;
    }
    else if (command == "provoke")
    {
        // as if the player killed enough gang members here, CGangWars::Update starts an offensive war
        CGangWars::Provocation = 3.0f;
    }
    else if (command == "shoot-wave")
    {
        int count = 100;
        stream >> count;
        ShootGangMembers(pPlayer, count);
    }
    else if (command == "turn")
    {
        // a sharp camera turn: the population must not depend on where the camera looks
        float fDegrees = 180.0f;
        stream >> fDegrees;
        CCam& cam = TheCamera.m_aCams[TheCamera.m_nActiveCam];
        cam.m_fHorizontalAngle += fDegrees * 3.14159265f / 180.0f;
        logger::info("stand debug: camera turned by %.0f", fDegrees);
    }
    else if (command == "pitch")
    {
        float fRadians = -1.4f;
        stream >> fRadians;
        TheCamera.m_aCams[TheCamera.m_nActiveCam].m_fVerticalAngle = fRadians;
        logger::info("stand debug: camera pitch %.2f", fRadians);
    }
    else if (command == "vehicles")
    {
        // vehicles by the 2D distance from the player, total / on screen
        int counts[5]{}, onScreen[5]{};
        const float limits[5] = {22.0f, 45.0f, 100.0f, 170.0f, 100000.0f};
        for (auto pVehicle : CPools::ms_pVehiclePool)
        {
            float fDist = (pVehicle->GetPosition() - pPlayer->GetPosition()).Magnitude2D();
            int i = 0;
            while (fDist > limits[i])
            {
                i++;
            }
            counts[i]++;
            onScreen[i] += pVehicle->GetIsOnScreen() ? 1 : 0;
        }
        logger::info("stand debug: vehicles <22 %d/%d <45 %d/%d <100 %d/%d <170 %d/%d far %d/%d", counts[0],
            onScreen[0], counts[1], onScreen[1], counts[2], onScreen[2], counts[3], onScreen[3], counts[4], onScreen[4]);
    }
    else if (command == "dupes")
    {
        // peds / vehicles of one model at one spot within 150 units: copies of a scenario ped or a parked car
        const CVector& center = pPlayer->GetPosition();
        int nPeds = 0, nVehicles = 0, nDupes = 0;
        auto logPed = [](CPed* pPed, const char* szWhat)
        {
            CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPed);
            CTask* pTask = pPed->m_pIntelligence->m_TaskMgr.GetActiveTask();
            const CVector& pos = pPed->GetPosition();
            logger::info("stand debug: dupes %s ped %s ours %d model %d type %d created %d task %d at %.2f %.2f %.2f",
                szWhat, pNetworkPed ? to_string(pNetworkPed->m_pedId).c_str() : "-",
                pNetworkPed ? pNetworkPed->m_bSyncing : -1, pPed->m_nModelIndex, static_cast<int>(pPed->m_nPedType),
                pPed->m_nCreatedBy, pTask ? pTask->GetTaskType() : -1, pos.x, pos.y, pos.z);
        };
        auto logVehicle = [](CVehicle* pVehicle, const char* szWhat)
        {
            CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle);
            const CVector& pos = pVehicle->GetPosition();
            logger::info("stand debug: dupes %s vehicle %s ours %d model %d created %d status %d at %.2f %.2f %.2f",
                szWhat, pNetworkVehicle ? to_string(pNetworkVehicle->m_vehicleId).c_str() : "-",
                pNetworkVehicle ? pNetworkVehicle->m_bSyncing : -1, pVehicle->m_nModelIndex, pVehicle->m_nCreatedBy,
                static_cast<int>(pVehicle->m_nStatus), pos.x, pos.y, pos.z);
        };
        std::vector<CPed*> peds;
        for (auto pPed : CPools::ms_pPedPool)
        {
            if (!pPed->IsPlayer() && !pPed->m_nPedFlags.bInVehicle &&
                (pPed->GetPosition() - center).Magnitude() < 150.0f)
            {
                peds.push_back(pPed);
            }
        }
        for (size_t i = 0; i < peds.size(); i++)
        {
            nPeds++;
            for (size_t j = i + 1; j < peds.size(); j++)
            {
                if (peds[i]->m_nModelIndex == peds[j]->m_nModelIndex &&
                    (peds[i]->GetPosition() - peds[j]->GetPosition()).Magnitude() < 1.5f)
                {
                    nDupes++;
                    logPed(peds[i], "pair");
                    logPed(peds[j], "with");
                }
            }
        }
        std::vector<CVehicle*> vehicles;
        for (auto pVehicle : CPools::ms_pVehiclePool)
        {
            if ((pVehicle->GetPosition() - center).Magnitude() < 150.0f)
            {
                vehicles.push_back(pVehicle);
            }
        }
        for (size_t i = 0; i < vehicles.size(); i++)
        {
            nVehicles++;
            for (size_t j = i + 1; j < vehicles.size(); j++)
            {
                if ((vehicles[i]->GetPosition() - vehicles[j]->GetPosition()).Magnitude() < 2.5f)
                {
                    nDupes++;
                    logVehicle(vehicles[i], "pair");
                    logVehicle(vehicles[j], "with");
                }
            }
        }
        if (stream >> std::ws && stream.peek() == 'a')  // "dupes all": every one of them
        {
            for (auto pPed : peds)
            {
                logPed(pPed, "all");
            }
            for (auto pVehicle : vehicles)
            {
                logVehicle(pVehicle, "all");
            }
        }
        logger::info("stand debug: dupes %d of %d peds %d vehicles", nDupes, nPeds, nVehicles);
    }
    else if (command == "objects")
    {
        LogMapObjects(pPlayer->GetPosition());
    }
    else if (command == "status")
    {
        const CVector& pos = pPlayer->GetPosition();
        CWanted* pWanted = FindPlayerWanted(0);
        logger::info("stand debug: player at %.2f %.2f %.2f health %.1f armour %.1f wanted %d", pos.x, pos.y, pos.z,
            pPlayer->m_fHealth, pPlayer->m_fArmour, pWanted ? pWanted->m_nWantedLevel : -1);
    }
    else if (command == "object-damage")
    {
        int count = 1;
        stream >> count;
        for (CObject* pObject : FindMapObjects(pPlayer->GetPosition(), 40.0f))
        {
            if (count <= 0)
            {
                break;
            }
            if (pObject->m_nColDamageEffect == 0 || !pObject->m_bUsesCollision)
            {
                continue;
            }

            CVector origin = pObject->GetPosition();
            CVector direction(0.0f, 0.0f, 1.0f);
            pObject->ObjectDamage(10000.0f, &origin, &direction, pPlayer, WEAPON_PISTOL);
            logger::info("stand debug: damaged object %d at %.2f %.2f %.2f", pObject->m_nModelIndex, origin.x, origin.y,
                origin.z);
            count--;
        }
    }
    else if (command == "object-push")
    {
        for (CObject* pObject : FindMapObjects(pPlayer->GetPosition(), 40.0f))
        {
            // not the fixed ones (infinite mass: fences, signs bolted down)
            if (!pObject->m_bUsesCollision || pObject->m_nPhysicalFlags.bInfiniteMass ||
                pObject->m_nPhysicalFlags.bDisableMoveForce || pObject->m_fMass >= 99999.0f)
            {
                continue;
            }

            pObject->m_pDamageEntity = pPlayer;
            pObject->SetIsStatic(false);
            pObject->AddToMovingList();
            pObject->m_vecMoveSpeed = CVector(0.1f, 0.05f, 0.15f);
            pObject->m_vecTurnSpeed = CVector(0.02f, 0.0f, 0.01f);
            logger::info("stand debug: pushed object %d at %.2f %.2f %.2f", pObject->m_nModelIndex,
                pObject->GetPosition().x, pObject->GetPosition().y, pObject->GetPosition().z);
            break;
        }
    }
    else if (command == "script-speed" || command == "script-cruise")
    {
        float speed = 0.0f;
        stream >> speed;
        if (CTrain* pTrain = FindNearestFrontCarriage(pPlayer->GetPosition(), true))
        {
            int handle = CPools::GetVehicleRef(pTrain);
            if (command == "script-speed")
            {
                plugin::Command<Commands::SET_TRAIN_SPEED>(handle, speed);
            }
            else
            {
                plugin::Command<Commands::SET_TRAIN_CRUISE_SPEED>(handle, speed);
            }
        }
    }
    else if (command == "car")
    {
        int model = 400;
        std::string driverless;
        stream >> model >> driverless;
        CVector pos = pPlayer->GetPosition() + pPlayer->GetForward() * 6.0f;
        plugin::Command<Commands::REQUEST_MODEL>(model);
        plugin::Command<Commands::LOAD_ALL_MODELS_NOW>();
        int car = 0;
        if (driverless == "random")
        {
            // not a script's one (CREATE_CAR makes a mission vehicle): as a parked car of the traffic
            CVehicle* pVehicle = new CAutomobile(model, RANDOM_VEHICLE, true);
            pVehicle->SetPosn(pos);
            CNetworkVehicle::CreateHosted(pVehicle);  // our CWorld::Add call is not one the world hook takes
            CWorld::Add(pVehicle);
            ms_nLastCar = CPools::GetVehicleRef(pVehicle);
            logger::info("stand debug: random car %d at %.2f %.2f %.2f", ms_nLastCar, pos.x, pos.y, pos.z);
        }
        else
        {
            plugin::Command<Commands::CREATE_CAR>(model, pos.x, pos.y, pos.z, &car);
            // a parked car with a driver, as a jacking needs
            int driver = 0;
            if (driverless != "empty")
            {
                plugin::Command<Commands::CREATE_RANDOM_CHAR_AS_DRIVER>(car, &driver);
                plugin::Command<Commands::CAR_SET_IDLE>(car);
            }
            logger::info("stand debug: car %d with driver %d at %.2f %.2f %.2f", car, driver, pos.x, pos.y, pos.z);
            ms_nLastCar = car;
        }
    }
    else if (command == "enter-driver")
    {
        // as the F key (TaskHooks sends the enter for a key press)
        if (CVehicle* pVehicle = FindNearestVehicle(pPlayer->GetPosition(), 30.0f))
        {
            if (CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle))
            {
                Packets::Vehicles::VehicleEnter packet{};
                packet.vehicleid = pNetworkVehicle->m_vehicleId;
                GetPacketFactory().Send(packet);
            }
            pPlayer->m_pIntelligence->m_TaskMgr.SetTask(new CTaskComplexEnterCarAsDriver(pVehicle), TASK_PRIMARY_PRIMARY, false);
        }
    }
    else if (command == "enter-passenger")
    {
        if (!CPassengerEnter::EnterNearestVehicle(pPlayer))
        {
            logger::info("stand debug: no free passenger door");
            if (CVehicle* pVehicle = FindNearestVehicle(pPlayer->GetPosition(), 30.0f))
            {
                // which of the checks of GetNearestCarPassengerDoor fails: occupied, door in use, no room
                CVector temp;
                int doorId = 0;
                bool bNone = CCarEnterExit::GetNearestCarPassengerDoor(pPlayer, pVehicle, &temp, &doorId, false, false, false);
                bool bOccupied = CCarEnterExit::GetNearestCarPassengerDoor(pPlayer, pVehicle, &temp, &doorId, true, false, false);
                bool bInUse = CCarEnterExit::GetNearestCarPassengerDoor(pPlayer, pVehicle, &temp, &doorId, false, true, false);
                bool bRoom = CCarEnterExit::GetNearestCarPassengerDoor(pPlayer, pVehicle, &temp, &doorId, false, false, true);
                logger::info("stand debug: nearest vehicle model %d network %d distance %.2f driver %p passenger0 %p "
                             "getting in %d in flags %x out flags %x; door found: none %d occupied %d in use %d room %d",
                    pVehicle->m_nModelIndex, CNetworkVehicleManager::GetVehicle(pVehicle) != nullptr,
                    (pVehicle->GetPosition() - pPlayer->GetPosition()).Magnitude(), pVehicle->m_pDriver,
                    pVehicle->m_apPassengers[0], pVehicle->m_nNumGettingIn, pVehicle->m_nGettingInFlags,
                    pVehicle->m_nGettingOutFlags, bNone, bOccupied, bInUse, bRoom);
            }
        }
    }
    else if (command == "script-enter")
    {
        // as a mission's TASK_ENTER_CAR_AS_PASSENGER replayed for us in a scene (SWEET2 after the blast): no control,
        // the task into the nearest vehicle; "control" keeps the control
        int seat = 1;
        int time = 50000;
        std::string mode;
        stream >> seat >> time >> mode;
        if (CVehicle* pVehicle = FindNearestVehicle(pPlayer->GetPosition(), 40.0f))
        {
            if (mode != "control")
            {
                plugin::Command<Commands::SET_PLAYER_CONTROL>(0, false);
            }
            plugin::Command<Commands::TASK_ENTER_CAR_AS_PASSENGER>(CPools::GetPedRef(pPlayer),
                CPools::GetVehicleRef(pVehicle), time, seat);  // the time, then the seat
            logger::info("stand debug: script enter into model %d seat %d time %d, %.1f m", pVehicle->m_nModelIndex, seat, time,
                (pVehicle->GetPosition() - pPlayer->GetPosition()).Magnitude());
        }
    }
    else if (command == "break-bottles")
    {
        // the mission objects near us hit by our shot (SWEET2's bottles: the script's HasBeenDamaged, ours too)
        int count = 0;
        for (auto pObject : CPools::ms_pObjectPool)
        {
            if (pObject && (pObject->m_nObjectType == OBJECT_MISSION || pObject->m_nObjectType == OBJECT_MISSION2) &&
                (pObject->GetPosition() - pPlayer->GetPosition()).Magnitude() < 40.0f)
            {
                pObject->ObjectDamage(10000.0f, nullptr, nullptr, pPlayer, WEAPON_PISTOL);
                count++;
            }
        }
        logger::info("stand debug: %d mission objects hit", count);
    }
    else if (command == "hud")
    {
        // what is on our screen now (CScreenLog): the texts, widescreen, the control, the camera, the counters, the
        // checkpoint and the blips
        CScreenLog::DumpState();
    }
    else if (command == "control")
    {
        plugin::Command<Commands::SET_PLAYER_CONTROL>(0, true);
    }
    else if (command == "warp-remote-passenger")
    {
        // as VEHICLE_PASSENGER_UPDATE does when it outruns the enter task here (a laggy connection)
        for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
        {
            if (pNetworkPlayer->m_pPed == nullptr)
            {
                continue;
            }
            CVehicle* pVehicle = FindNearestVehicle(pNetworkPlayer->m_pPed->GetPosition(), 10.0f);
            if (pVehicle == nullptr)
            {
                continue;
            }

            // the same check as the handler
            bool bEntering = pNetworkPlayer->IsEnteringVehicle(pVehicle);
            logger::info("stand debug: %s entering %d", pNetworkPlayer->GetName().c_str(), bEntering);
            if (!bEntering)
            {
                pNetworkPlayer->WarpIntoVehiclePassenger(pVehicle, 0);
            }
        }
    }
    else if (command == "watch-vehicle")
    {
        stream >> ms_watchedVehicle;
    }
    else if (command == "blow" || command == "vehicle-health" || command == "vehicle-state" || command == "burst" ||
             command == "explode-car" || command == "siren" || command == "alarm" || command == "force-lights")
    {
        // the watched vehicle (watch-vehicle <id>), else the last `car`, else the nearest one: blows up through the
        // game's own BlowUpCar (CVehicleBlowSync takes it), gets a health (below 250 it burns, then blows up by its
        // fire timer), logs its state
        CVehicle* pVehicle = nullptr;
        for (CNetworkVehicle* pWatched : CNetworkVehicleManager::m_pVehicles)
        {
            if (to_string(pWatched->m_vehicleId) == ms_watchedVehicle)
            {
                pVehicle = pWatched->m_pVehicle;
            }
        }
        if (!pVehicle)
        {
            pVehicle = CPools::GetVehicle(ms_nLastCar);
        }
        if (!pVehicle)
        {
            pVehicle = FindNearestVehicle(pPlayer->GetPosition(), 30.0f);
        }
        CNetworkVehicle* pNetworkVehicle = pVehicle ? CNetworkVehicleManager::GetVehicle(pVehicle) : nullptr;
        if (!pNetworkVehicle)
        {
            logger::info("stand debug: no network vehicle near");
        }
        else if (command == "blow")
        {
            pVehicle->BlowUpCar(pPlayer, false);
        }
        else if (command == "burst")
        {
            // as a spike strip or a shot: the game's own BurstTyre, 13..16 are the wheels 0..3 (0x6A32E6)
            int wheel = 13;
            stream >> wheel;
            pVehicle->BurstTyre(static_cast<unsigned char>(wheel), true);
        }
        else if (command == "explode-car")
        {
            // a small explosion (a grenade) right next to it, here only (explosions are not synced)
            CVector pos = pVehicle->GetPosition() + pVehicle->GetRight() * 3.0f;
            CExplosion::AddExplosion(nullptr, pPlayer, EXPLOSION_GRENADE, pos, 0, true, -1.0f, false);
        }
        else if (command == "siren")
        {
            int on = 1;
            stream >> on;
            pVehicle->m_nVehicleFlags.bSirenOrAlarm = on != 0;
        }
        else if (command == "alarm")
        {
            pVehicle->m_nAlarmState = 8000;  // rings 8 s, as a pulled door of a locked car
        }
        else if (command == "force-lights")
        {
            int mode = 2;
            stream >> mode;
            plugin::Command<Commands::FORCE_CAR_LIGHTS>(CPools::GetVehicleRef(pVehicle), mode);
        }
        else if (command == "vehicle-health")
        {
            float health = 1000.0f;
            stream >> health;
            pVehicle->m_fHealth = health;
        }
        if (pNetworkVehicle)
        {
            CVector pos = pVehicle->GetPosition();
            // the tyres of a car (CDamageManager wheel status: 0 ok, 1 burst, 2 missing), front left first
            char szWheels[5] = "----";
            unsigned int nPanels = 0;  // 4 bits a panel, the windscreen is panel 4
            if (pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
            {
                CDamageManager& damage = static_cast<CAutomobile*>(pVehicle)->m_damageManager;
                nPanels = damage.m_nPanelsStatus;
                for (int i = 0; i < 4; i++)
                {
                    szWheels[i] = static_cast<char>('0' + damage.GetWheelStatus(i));
                }
            }
            logger::info("stand debug: vehicle %s syncing %d status %d health %.0f wheels %s panels %07x siren %d "
                         "alarm %d lights %d at %.2f %.2f %.2f",
                to_string(pNetworkVehicle->m_vehicleId).c_str(), pNetworkVehicle->m_bSyncing, pVehicle->m_nStatus,
                pVehicle->m_fHealth, szWheels, nPanels, pVehicle->m_nVehicleFlags.bSirenOrAlarm, pVehicle->m_nAlarmState,
                pVehicle->m_nOverrideLights, pos.x, pos.y, pos.z);
        }
    }
    else if (command == "push-vehicle")
    {
        // the last `car` (ours, empty) is thrown at the watched vehicle, speed in units per frame (0.2: 36 km/h)
        float speed = 0.2f;
        stream >> speed;
        CVehicle* pOurs = CPools::GetVehicle(ms_nLastCar);
        CVehicle* pTarget = nullptr;
        for (CNetworkVehicle* pWatched : CNetworkVehicleManager::m_pVehicles)
        {
            if (to_string(pWatched->m_vehicleId) == ms_watchedVehicle)
            {
                pTarget = pWatched->m_pVehicle;
            }
        }
        if (pOurs && pTarget && pOurs != pTarget)
        {
            CVector direction = pTarget->GetPosition() - pOurs->GetPosition();
            direction.z = 0.0f;
            direction.Normalise();
            pOurs->m_vecMoveSpeed = direction * speed;
        }
    }
    else if (command == "knock-off")
    {
        // as CBike::DamageKnockOffRider (0x6B5A10) does on a crash: the event, the response throws the rider
        CVehicle* pVehicle = pPlayer->m_nPedFlags.bInVehicle ? pPlayer->m_pVehicle : nullptr;
        if (pVehicle)
        {
            alignas(4) uint8_t event[0x3C];
            CVector moveSpeed = pVehicle->m_vecMoveSpeed;
            CVector impact = moveSpeed;
            // CEventKnockOffBike::CEventKnockOffBike, KNOCK_OFF_TYPE_SKIDBACKFRONT, forced
            plugin::CallMethod<0x4AFCF0, void*, CVehicle*, CVector*, CVector*, float, float, uint8_t, uint8_t, int, CPed*,
                bool, bool>(event, pVehicle, &moveSpeed, &impact, 100.0f, 0.0f, 49, 0, 0, nullptr, true, true);
            // CEventGroup::Add (a copy), the group is CPedIntelligence + 0x68
            plugin::CallMethod<0x4AB420, void*, void*, bool>(reinterpret_cast<uint8_t*>(pPlayer->m_pIntelligence) + 0x68,
                event, false);
            using Dtor = void(__thiscall*)(void*, int);
            (*reinterpret_cast<Dtor**>(event))[0](event, 0);  // the virtual destructor, not freed (on the stack)
        }
    }
    else if (command == "leave")
    {
        // as the exit key, at any speed (a fast vehicle: CTaskSimpleCarJumpOut)
        float speed = 0.0f;
        stream >> speed;
        CVehicle* pVehicle = pPlayer->m_nPedFlags.bInVehicle ? pPlayer->m_pVehicle : nullptr;
        if (pVehicle)
        {
            if (speed > 0.0f)
            {
                pVehicle->m_vecMoveSpeed = pVehicle->GetForward() * speed;
            }
            CVehicleExitSync::SendLeave(pVehicle);
            pPlayer->m_pIntelligence->m_TaskMgr.SetTask(new CTaskComplexLeaveCar(pVehicle, 0, 0, true, false),
                TASK_PRIMARY_PRIMARY, false);
        }
    }
    else if (command == "flush")
    {
        // CLEAR_CHAR_TASKS_IMMEDIATELY: CTaskSimpleCarDrive::MakeAbortable sets the ped out at once
        pPlayer->m_pIntelligence->FlushImmediately(true);
    }
    else if (command == "jack-me")
    {
        // a random ped next to our vehicle drags us out
        CVehicle* pVehicle = pPlayer->m_nPedFlags.bInVehicle ? pPlayer->m_pVehicle : nullptr;
        if (pVehicle)
        {
            pVehicle->m_vecMoveSpeed = CVector(0.0f, 0.0f, 0.0f);
            CVector pos = pVehicle->GetPosition() - pVehicle->GetRight() * 3.0f;
            int ped = 0;
            plugin::Command<Commands::CREATE_RANDOM_CHAR>(pos.x, pos.y, pos.z, &ped);
            plugin::Command<Commands::TASK_ENTER_CAR_AS_DRIVER>(ped, CPools::GetVehicleRef(pVehicle), 20000);
            logger::info("stand debug: jacker %d", ped);
        }
    }
    else if (command == "ram-me")
    {
        // an empty car 12 m ahead rolls into the player
        float speed = 0.6f;
        stream >> speed;
        CVector forward = pPlayer->GetForward();
        CVector pos = pPlayer->GetPosition() + forward * 12.0f;
        plugin::Command<Commands::REQUEST_MODEL>(400);
        plugin::Command<Commands::LOAD_ALL_MODELS_NOW>();
        int car = 0;
        plugin::Command<Commands::CREATE_CAR>(400, pos.x, pos.y, pos.z, &car);
        if (CVehicle* pVehicle = CPools::GetVehicle(car))
        {
            pVehicle->SetHeading(atan2f(forward.x, -forward.y) + PI);
            pVehicle->m_vecMoveSpeed = forward * -speed;
            logger::info("stand debug: ram car %d", car);
        }
    }
    else if (command == "ram-ped")
    {
        // an empty car rolls into the nearest ped clone: only its owner may knock it down
        float speed = 0.6f;
        stream >> speed;
        CNetworkPed* pTarget = nullptr;
        float fNearest = 40.0f;
        for (auto pNetworkPed : CNetworkPedManager::m_pPeds)
        {
            // the watched ped (watch-ped) first
            if (!ms_watchedPed.empty() && to_string(pNetworkPed->m_pedId) == ms_watchedPed && pNetworkPed->m_pPed)
            {
                pTarget = pNetworkPed;
                break;
            }
            CPed* pPed = pNetworkPed->m_pPed;
            if (!pNetworkPed->m_bSyncing && pPed && !pPed->m_nPedFlags.bInVehicle &&
                (pPed->GetPosition() - pPlayer->GetPosition()).Magnitude() < fNearest)
            {
                fNearest = (pPed->GetPosition() - pPlayer->GetPosition()).Magnitude();
                pTarget = pNetworkPed;
            }
        }
        if (pTarget)
        {
            CVector target = pTarget->m_pPed->GetPosition();
            CVector dir = target - pPlayer->GetPosition();
            dir.z = 0.0f;
            dir.Normalise();
            CVector pos = target - dir * 7.0f;
            plugin::Command<Commands::REQUEST_MODEL>(400);
            plugin::Command<Commands::LOAD_ALL_MODELS_NOW>();
            int car = 0;
            plugin::Command<Commands::CREATE_CAR>(400, pos.x, pos.y, target.z, &car);
            if (CVehicle* pVehicle = CPools::GetVehicle(car))
            {
                pVehicle->SetHeading(atan2f(-dir.x, dir.y));
                pVehicle->m_vecMoveSpeed = dir * speed;
            }
            ms_watchedPed = to_string(pTarget->m_pedId);
            logger::info("stand debug: ram ped %s", ms_watchedPed.c_str());
        }
    }
    else if (command == "kill-me")
    {
        // CWeapon::GenerateDamageEvent, as a shot that kills: the damage response, the die task, PED_DIED
        plugin::Call<0x73A530, CPed*, CEntity*, int, int, int, unsigned char>(pPlayer, nullptr, WEAPON_PISTOL, 1000, 3, 0);
    }
    else if (command == "knife")
    {
        pPlayer->GiveWeapon(WEAPON_KNIFE, 1, true);
        pPlayer->SetCurrentWeapon(WEAPON_KNIFE);
    }
    else if (command == "stab")
    {
        // behind the nearest ped (a clone with "clone"), the stealth kill CTaskSimplePlayerOnFoot starts (0x6880F0)
        std::string which;
        stream >> which;
        CPed* pTarget = nullptr;
        float fNearest = 80.0f;
        for (auto pNetworkPed : CNetworkPedManager::m_pPeds)
        {
            CPed* pPed = pNetworkPed->m_pPed;
            if (pPed && pPed->IsAlive() && !pPed->m_nPedFlags.bInVehicle && (which != "clone" || !pNetworkPed->m_bSyncing) &&
                (pPed->GetPosition() - pPlayer->GetPosition()).Magnitude() < fNearest)
            {
                fNearest = (pPed->GetPosition() - pPlayer->GetPosition()).Magnitude();
                pTarget = pPed;
            }
        }
        if (pTarget)
        {
            CVector forward = pTarget->GetForward();
            pPlayer->SetPosn(pTarget->GetPosition() - forward * 0.9f);
            pPlayer->m_fCurrentRotation = pPlayer->m_fAimingRotation = pTarget->m_fCurrentRotation;
            pPlayer->SetHeading(pTarget->m_fCurrentRotation);
            int nGroup = static_cast<int>(CWeaponInfo::GetWeaponInfo(WEAPON_KNIFE, 1)->m_nAnimToPlay);
            CTask* pTask = static_cast<CTask*>(CTask::operator new(0x20));
            plugin::CallMethod<0x6225F0, CTask*, bool, CPed*, int>(pTask, true, pTarget, nGroup);  // CTaskSimpleStealthKill ctor
            pPlayer->m_pIntelligence->m_TaskMgr.SetTask(pTask, TASK_PRIMARY_PRIMARY, false);
            plugin::Call<0x73A530, CPed*, CEntity*, int, int, int, unsigned char>(pTarget, pPlayer, WEAPON_KNIFE, 0, 3, 0);
            logger::info("stand debug: stab %s", CNetworkPedManager::GetPed(pTarget)
                                                     ? to_string(CNetworkPedManager::GetPed(pTarget)->m_pedId).c_str()
                                                     : "?");
        }
    }
    else if (command == "select-driver")
    {
        // the npc driver of the nearest vehicle becomes the watched ped (then npc-* acts on it, the other client watches it)
        CVehicle* pVehicle = CPools::GetVehicle(ms_nLastCar);  // the one `car` made, else the nearest
        if (!pVehicle || !pVehicle->m_pDriver)
        {
            pVehicle = FindNearestVehicle(pPlayer->GetPosition(), 30.0f);
        }
        CNetworkPed* pNetworkPed = pVehicle && pVehicle->m_pDriver ? CNetworkPedManager::GetPed(pVehicle->m_pDriver) : nullptr;
        if (pNetworkPed)
        {
            ms_watchedPed = to_string(pNetworkPed->m_pedId);
            logger::info("stand debug: driver %s", ms_watchedPed.c_str());
        }
    }
    else if (command == "npc-leave" || command == "npc-knock" || command == "npc-flush")
    {
        // the npc driver we sync of the nearest vehicle (car [model]): leaves it (pushed forward: rolls out), is
        // knocked off it, is set out at once
        CVehicle* pVehicle = CPools::GetVehicle(ms_nLastCar);
        if (!pVehicle || !pVehicle->m_pDriver)
        {
            pVehicle = FindNearestVehicle(pPlayer->GetPosition(), 30.0f);
        }
        CPed* pDriver = pVehicle ? pVehicle->m_pDriver : nullptr;
        CNetworkPed* pNetworkPed = pDriver ? CNetworkPedManager::GetPed(pDriver) : nullptr;
        if (!pNetworkPed || !pNetworkPed->m_bSyncing)
        {
            logger::info("stand debug: no npc driver of ours near");
        }
        else if (command == "npc-leave")
        {
            float speed = 0.0f;
            stream >> speed;
            if (speed > 0.0f)
            {
                pVehicle->m_vecMoveSpeed = pVehicle->GetForward() * speed;
            }
            pDriver->m_pIntelligence->m_TaskMgr.SetTask(new CTaskComplexLeaveCar(pVehicle, 0, 0, true, false),
                TASK_PRIMARY_PRIMARY, false);
        }
        else if (command == "npc-knock")
        {
            alignas(4) uint8_t event[0x3C];
            CVector moveSpeed = pVehicle->m_vecMoveSpeed;
            CVector impact = moveSpeed;
            plugin::CallMethod<0x4AFCF0, void*, CVehicle*, CVector*, CVector*, float, float, uint8_t, uint8_t, int, CPed*,
                bool, bool>(event, pVehicle, &moveSpeed, &impact, 100.0f, 0.0f, 49, 0, 0, nullptr, true, true);
            plugin::CallMethod<0x4AB420, void*, void*, bool>(reinterpret_cast<uint8_t*>(pDriver->m_pIntelligence) + 0x68,
                event, false);
            using Dtor = void(__thiscall*)(void*, int);
            (*reinterpret_cast<Dtor**>(event))[0](event, 0);
        }
        else
        {
            pDriver->m_pIntelligence->FlushImmediately(true);
        }
        if (pNetworkPed && pNetworkPed->m_bSyncing)
        {
            ms_watchedPed = to_string(pNetworkPed->m_pedId);
            logger::info("stand debug: %s %s", command.c_str(), ms_watchedPed.c_str());
        }
    }
    else if (command == "ped-freeze")
    {
        // off: the clones keep their physics (the baseline to compare with)
        std::string state;
        stream >> state;
        CNetworkPed::ms_bFreezeClones = state != "off";
    }
    else if (command == "position-sync")
    {
        std::string way;
        stream >> way;
        CPlayerPositionSync::ms_bOldWay = way == "old";
    }
    else if (command == "walk-to")
    {
        // the player walks (or runs) straight there by itself, as a script makes it
        float dx = 0.0f, dy = 0.0f;
        std::string run;
        stream >> dx >> dy >> run;
        CVector pos = pPlayer->GetPosition();
        plugin::Command<Commands::TASK_GO_STRAIGHT_TO_COORD>(CPools::GetPedRef(pPlayer), pos.x + dx, pos.y + dy, pos.z,
            run == "run" ? PEDMOVE_RUN : PEDMOVE_WALK, 20000);
    }
    else if (command == "watch-moves")
    {
        // per frame moves of the remote players' clones: smooth is a steady move, a jerk is far off the mean
        int ms = 5000;
        stream >> ms;
        ms_nWatchMovesUntil = CTimer::m_snTimeInMilliseconds + ms;
        ms_moves.clear();
        ms_nSnapsAtStart = CPlayerPositionSync::ms_nSnaps;
    }
    else if (command == "delete-car")
    {
        // the script way (DELETE_CAR) on the watched vehicle, ours or a clone
        for (CNetworkVehicle* pWatched : CNetworkVehicleManager::m_pVehicles)
        {
            if (to_string(pWatched->m_vehicleId) == ms_watchedVehicle && pWatched->m_pVehicle)
            {
                int car = CPools::GetVehicleRef(pWatched->m_pVehicle);
                plugin::Command<Commands::DELETE_CAR>(car);
                logger::info("stand debug: deleted car %d", car);
                break;
            }
        }
    }
    else if (command == "tow")
    {
        // a tractor in front of the watched vehicle (a trailer), the player drives it, the trailer hitched at once;
        // then a push forward (units per frame)
        float speed = 0.3f;
        int model = 515;  // roadtrain
        stream >> speed >> model;
        CVehicle* pTrailer = nullptr;
        for (CNetworkVehicle* pWatched : CNetworkVehicleManager::m_pVehicles)
        {
            if (to_string(pWatched->m_vehicleId) == ms_watchedVehicle)
            {
                pTrailer = pWatched->m_pVehicle;
            }
        }
        if (!pTrailer)
        {
            logger::info("stand debug: no watched trailer");
            return;
        }
        plugin::Command<Commands::REQUEST_MODEL>(model);
        plugin::Command<Commands::LOAD_ALL_MODELS_NOW>();
        CVector pos = pTrailer->GetPosition() + pTrailer->GetForward() * 10.0f;
        int car = 0;
        plugin::Command<Commands::CREATE_CAR>(model, pos.x, pos.y, pos.z + 1.0f, &car);
        CVehicle* pTractor = CPools::GetVehicle(car);
        if (!pTractor)
        {
            return;
        }
        pTractor->SetHeading(pTrailer->GetHeading());
        plugin::Command<Commands::WARP_CHAR_INTO_CAR>(CPools::GetPedRef(pPlayer), car);
        pTrailer->SetTowLink(pTractor, true);
        pTractor->m_vecMoveSpeed = pTractor->GetForward() * speed;
        ms_nLastCar = car;
        logger::info("stand debug: tow car %d, trailer towed %d", car, pTrailer->m_pTractor == pTractor);
    }
    else if (command == "ride")
    {
        // a car with an npc driver wanders, the player stands on its roof
        int model = 455;
        stream >> model;
        CVector pos = pPlayer->GetPosition() + pPlayer->GetForward() * 6.0f;
        plugin::Command<Commands::REQUEST_MODEL>(model);
        plugin::Command<Commands::LOAD_ALL_MODELS_NOW>();
        int car = 0, driver = 0;
        plugin::Command<Commands::CREATE_CAR>(model, pos.x, pos.y, pos.z, &car);
        plugin::Command<Commands::CREATE_RANDOM_CHAR_AS_DRIVER>(car, &driver);
        plugin::Command<Commands::SET_CAR_CRUISE_SPEED>(car, 8.0f);
        plugin::Command<Commands::CAR_WANDER_RANDOMLY>(car);
        if (CVehicle* pVehicle = CPools::GetVehicle(car))
        {
            CColModel* pCol = pVehicle->GetColModel();
            float fTop = pCol ? pCol->m_boundBox.m_vecMax.z : 2.0f;
            pPlayer->SetPosn(pVehicle->GetPosition() + CVector(0.0f, 0.0f, fTop + 1.0f));
            logger::info("stand debug: ride car %d", car);
        }
    }
    else if (command == "burn-me")
    {
        plugin::CallMethod<0x53A050, CFireManager*, CEntity*, CEntity*, float, uint8_t, unsigned int, int8_t>(
            &gFireManager, pPlayer, nullptr, 0.8f, 1, 7000, 0);  // CFireManager::StartFire(CEntity*)
    }
    else if (command == "gas-me")
    {
        // the task the damage response of tear gas gives (a tear gas damage event alone doesn't make the player choke)
        CTask* pTask = static_cast<CTask*>(CTask::operator new(0x1C));
        plugin::CallMethod<0x6202C0, CTask*, CPed*, bool>(pTask, nullptr, true);  // CTaskSimpleChoking ctor
        pPlayer->m_pIntelligence->m_TaskMgr.SetTask(pTask, TASK_PRIMARY_PHYSICAL_RESPONSE, false);
    }
    else if (command == "climb")
    {
        // turn round until CTaskSimpleClimb::TestForClimb finds a handhold, climb it (as CTaskComplexJump does)
        float fHeading = pPlayer->m_fCurrentRotation;
        for (int i = 0; i < 12; i++)
        {
            float fTry = fHeading + i * (2.0f * PI / 12.0f);
            pPlayer->m_fCurrentRotation = pPlayer->m_fAimingRotation = fTry;
            pPlayer->SetHeading(fTry);
            pPlayer->UpdateRwMatrix();
            CVector climbPos{};
            float fClimbHeading = 0.0f;
            uint8_t surface = 0;
            CEntity* pEntity = plugin::CallAndReturn<CEntity*, 0x6803A0, CPed*, CVector&, float&, uint8_t&, bool>(
                pPlayer, climbPos, fClimbHeading, surface, true);
            if (pEntity)
            {
                CTask* pTask = static_cast<CTask*>(CTask::operator new(0x30));
                plugin::CallMethod<0x67A110, CTask*, CEntity*, CVector*, float, uint8_t, int8_t, bool>(pTask, pEntity,
                    &climbPos, fClimbHeading, surface, 1, false);  // CTaskSimpleClimb ctor, CLIMB_GRAB
                pPlayer->m_pIntelligence->m_TaskMgr.SetTask(pTask, TASK_PRIMARY_PRIMARY, false);
                logger::info("stand debug: climb at heading %.2f", fTry);
                break;
            }
        }
    }
    else if (command == "spawn-ped")
    {
        // a random ped 3 m ahead that stands still for a minute (a target that doesn't move by itself)
        CVector pos = pPlayer->GetPosition() + pPlayer->GetForward() * 3.0f;
        int ped = 0;
        plugin::Command<Commands::CREATE_RANDOM_CHAR>(pos.x, pos.y, pos.z, &ped);
        plugin::Command<Commands::TASK_STAND_STILL>(ped, 60000);
        CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(CPools::GetPed(ped));
        logger::info("stand debug: spawned ped %s", pNetworkPed ? to_string(pNetworkPed->m_pedId).c_str() : "?");
    }
    else if (command == "watch-error")
    {
        // how far the watched ped clone is from its owner's position, every frame
        int ms = 6000;
        stream >> ms;
        ms_nWatchErrorUntil = CTimer::m_snTimeInMilliseconds + ms;
        ms_errors.clear();
    }
    else if (command == "face-watched")
    {
        // the player and the camera behind it turn to the watched ped (for screenshots)
        for (auto pNetworkPed : CNetworkPedManager::m_pPeds)
        {
            if (pNetworkPed->m_pPed && to_string(pNetworkPed->m_pedId) == ms_watchedPed)
            {
                CVector dir = pNetworkPed->m_pPed->GetPosition() - pPlayer->GetPosition();
                float fHeading = atan2f(-dir.x, dir.y);
                pPlayer->m_fCurrentRotation = pPlayer->m_fAimingRotation = fHeading;
                pPlayer->SetHeading(fHeading);
                TheCamera.SetCameraDirectlyBehindForFollowPed_CamOnAString();
                TheCamera.RestoreWithJumpCut();
            }
        }
    }
    else if (command == "walk-into")
    {
        // the player runs through the watched ped (3 m past it)
        for (auto pNetworkPed : CNetworkPedManager::m_pPeds)
        {
            if (pNetworkPed->m_pPed && to_string(pNetworkPed->m_pedId) == ms_watchedPed)
            {
                CVector target = pNetworkPed->m_pPed->GetPosition();
                CVector dir = target - pPlayer->GetPosition();
                dir.z = 0.0f;
                dir.Normalise();
                target += dir * 3.0f;
                plugin::Command<Commands::TASK_GO_STRAIGHT_TO_COORD>(CPools::GetPedRef(pPlayer), target.x, target.y,
                    target.z, PEDMOVE_SPRINT, 10000);
            }
        }
    }
    else if (command == "watch-ped")
    {
        stream >> ms_watchedPed;
    }
    else if (command == "g")
    {
        ms_bPressG = true;
    }
    // the running cutscene, as the skip button does (the host: the script sees it finished)
    else if (command == "skip-cutscene")
    {
        if (!CCutsceneMgr::ms_running)
        {
            logger::info("stand debug: no cutscene");
            return;
        }
        CCutsceneMgr::SkipCutscene();
    }
    // the weapons in the hands drawn without the depth test (RenderWeaponPedsForPC: ZTESTENABLE 1 -> 0): seen through
    // whatever covers them
    else if (command == "weapons-noz")
    {
        int nOn = 1;
        stream >> nOn;
        patch::SetUChar(0x732F40, nOn ? 0 : 1);
    }
    else if (command == "watch-slopes")
    {
        int ms = 20000;
        stream >> ms;
        ms_nWatchSlopesUntil = CTimer::m_snTimeInMilliseconds + ms;
        ms_slopes.clear();
    }
    else if (command == "watch-anims")
    {
        int ms = 10000;
        stream >> ms;
        ms_nWatchAnimsUntil = CTimer::m_snTimeInMilliseconds + ms;
    }
    else
    {
        logger::warn("stand debug: unknown command '%s'", line.c_str());
        return;
    }

    logger::info("stand debug: %s", line.c_str());
}

void CDebugCommands::Process()
{
    if (!CCore::ms_bDebug)
    {
        return;
    }

    // again every frame: the scripts set the maximum back (the city borders, the missions)
    if (ms_bPoliceOff)
    {
        CWanted::SetMaximumWantedLevel(0);
        CWanted* pWanted = FindPlayerWanted(0);
        if (pWanted && pWanted->m_nWantedLevel > 0)
        {
            pWanted->ClearWantedLevelAndGoOnParole();
        }
    }

    if (CTimer::m_snTimeInMilliseconds < ms_nWatchAnimsUntil)
    {
        LogPlayerAnims();
    }

    // watch-error: the watched ped clone's distance to the position of its last update
    if (ms_nWatchErrorUntil != 0)
    {
        for (auto pNetworkPed : CNetworkPedManager::m_pPeds)
        {
            if (!pNetworkPed->m_bSyncing && pNetworkPed->m_pPed && pNetworkPed->m_bHasTargetPos &&
                to_string(pNetworkPed->m_pedId) == ms_watchedPed)
            {
                ms_errors.push_back((pNetworkPed->m_pPed->GetPosition() - pNetworkPed->m_vecTargetPos).Magnitude());
            }
        }
        if (CTimer::m_snTimeInMilliseconds >= ms_nWatchErrorUntil)
        {
            ms_nWatchErrorUntil = 0;
            float fSum = 0.0f, fMax = 0.0f;
            int nFar = 0;
            for (float fError : ms_errors)
            {
                fSum += fError;
                fMax = std::max(fMax, fError);
                nFar += fError > 0.5f;
            }
            logger::info("stand debug: error of %s: %d frames mean %.3f max %.3f over 0.5 m %d", ms_watchedPed.c_str(),
                static_cast<int>(ms_errors.size()), ms_errors.empty() ? 0.0f : fSum / ms_errors.size(), fMax, nFar);
        }
    }

    if (ms_nWatchSlopesUntil != 0)
    {
        ProcessWatchSlopes();
    }

    // watch-moves: a frame's move of the first remote player's clone, and its distance to the vehicle under it
    if (ms_nWatchMovesUntil != 0)
    {
        CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::m_pPlayers.empty() ? nullptr : CNetworkPlayerManager::m_pPlayers[0];
        CPed* pClone = pNetworkPlayer ? pNetworkPlayer->m_pPed : nullptr;
        if (pClone)
        {
            CVector pos = pClone->GetPosition();
            CNetworkVehicle* pContact = CNetworkVehicleManager::GetVehicle(pNetworkPlayer->m_targetContactVehicle);
            float fToVehicle = pContact && pContact->m_pVehicle ? (pos - pContact->m_pVehicle->GetPosition()).Magnitude() : -1.0f;
            if (ms_bHasLastMove)
            {
                ms_moves.push_back({(pos - ms_vecLastMove).Magnitude(), fToVehicle});
                if (pContact && pContact->m_pVehicle)
                {
                    ms_fVehicleMoves += (pContact->m_pVehicle->GetPosition() - ms_vecLastVehicle).Magnitude();
                }
            }
            ms_vecLastMove = pos;
            if (pContact && pContact->m_pVehicle)
            {
                ms_vecLastVehicle = pContact->m_pVehicle->GetPosition();
            }
            ms_bHasLastMove = true;
        }

        if (CTimer::m_snTimeInMilliseconds >= ms_nWatchMovesUntil)
        {
            ms_nWatchMovesUntil = 0;
            ms_bHasLastMove = false;
            float fSum = 0.0f, fMax = 0.0f, fToMin = 1e9f, fToMax = -1.0f;
            for (auto& move : ms_moves)
            {
                fSum += move.first;
                fMax = std::max(fMax, move.first);
                if (move.second >= 0.0f)
                {
                    fToMin = std::min(fToMin, move.second);
                    fToMax = std::max(fToMax, move.second);
                }
            }
            float fMean = ms_moves.empty() ? 0.0f : fSum / ms_moves.size();
            int nJerks = 0;
            for (auto& move : ms_moves)
            {
                nJerks += move.first > fMean * 2.5f + 0.02f;
            }
            logger::info("stand debug: moves %d frames mean %.3f max %.3f jerks %d snaps %u to vehicle %.2f..%.2f "
                         "(vehicle moved %.1f m)",
                static_cast<int>(ms_moves.size()), fMean, fMax, nJerks, CPlayerPositionSync::ms_nSnaps - ms_nSnapsAtStart,
                fToMax < 0.0f ? -1.0f : fToMin, fToMax, ms_fVehicleMoves);
            ms_fVehicleMoves = 0.0f;
        }
    }

    // after CGame::Process (its CPad::UpdatePads would overwrite it), before CPassengerEnter::Process reads it
    if (ms_bPressG)
    {
        ms_bPressG = false;
        CPad* pPad = CPad::GetPad(0);
        pPad->OldState.DPadUp = 0;
        pPad->NewState.DPadUp = 255;
    }

    // the commands of the stand hub answer with the lines they log
    for (const auto& [id, command] : CStandAgent::TakeCommands())
    {
        std::string output;
        logger::ms_pCapture = &output;
        RunCommand(command);
        logger::ms_pCapture = nullptr;
        CStandAgent::Reply(id, output);
    }

    static unsigned int nLastCheck = 0;
    if (CTimer::m_snTimeInMilliseconds - nLastCheck < 500)
    {
        return;
    }
    nLastCheck = CTimer::m_snTimeInMilliseconds;

    char szPath[64];
    sprintf_s(szPath, "CoopAndreas\\debug-cmd%d.txt", CCore::ms_nRunIndex);

    std::ifstream file(szPath);
    if (!file)
    {
        return;
    }

    std::string line;
    std::vector<std::string> lines;
    while (std::getline(file, line))
    {
        if (!line.empty())
        {
            lines.push_back(line);
        }
    }
    file.close();
    std::remove(szPath);

    for (const auto& command : lines)
    {
        RunCommand(command);
    }
}

// watch-slopes: every frame, the height of an npc driven vehicle over the ground under it, put by the slope it drives
// on (its forward axis pitched by more than 4 degrees, up or down by the way it goes). Each vehicle against its own
// flat road height: a clone that scrapes up a hill is lower, one that flies off down a hill higher
void CDebugCommands::ProcessWatchSlopes()
{
    CPlayerPed* pPlayer = FindPlayerPed(0);
    for (auto pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
        if (!pVehicle || !pVehicle->m_matrix || pNetworkVehicle->m_bTrain || !pVehicle->m_pDriver ||
            pVehicle->m_pDriver->IsPlayer() || pVehicle->m_nVehicleType != VEHICLE_AUTOMOBILE ||
            (pVehicle->GetPosition() - pPlayer->GetPosition()).Magnitude() > 80.0f)
        {
            continue;
        }

        CVector pos = pVehicle->GetPosition();
        CVector forward = pVehicle->m_matrix->up;
        CVector speed = pVehicle->m_vecMoveSpeed;
        float fFlatSpeed = speed.x * forward.x + speed.y * forward.y;
        if (std::fabs(fFlatSpeed) < 0.05f)
        {
            continue;
        }
        bool bFound = false;
        float fGround = CWorld::FindGroundZFor3DCoord(pos.x, pos.y, pos.z + 1.0f, &bFound, nullptr);
        if (!bFound)
        {
            continue;
        }

        float fClimb = fFlatSpeed > 0.0f ? forward.z : -forward.z;
        int nSlope = std::fabs(fClimb) < 0.03f ? 0 : fClimb > 0.07f ? 1 : fClimb < -0.07f ? 2 : -1;
        if (nSlope < 0)
        {
            continue;
        }

        auto it = std::find_if(ms_slopes.begin(), ms_slopes.end(), [pVehicle](auto& e) { return e.first == pVehicle; });
        if (it == ms_slopes.end())
        {
            ms_slopes.push_back({pVehicle, {}});
            it = ms_slopes.end() - 1;
        }
        it->second.bClone = !pNetworkVehicle->m_bSyncing;
        it->second.heights[nSlope].push_back(pos.z - fGround);
    }

    if (CTimer::m_snTimeInMilliseconds < ms_nWatchSlopesUntil)
    {
        return;
    }
    ms_nWatchSlopesUntil = 0;

    for (int nClone = 0; nClone < 2; nClone++)
    {
        int nFrames[3] = {};
        float fSum[3] = {}, fMin[3] = {1000.0f, 1000.0f, 1000.0f}, fMax[3] = {-1000.0f, -1000.0f, -1000.0f};
        for (auto& [pVehicle, samples] : ms_slopes)
        {
            if (samples.bClone != (nClone == 1) || samples.heights[0].size() < 10)
            {
                continue;
            }
            float fFlat = 0.0f;
            for (float h : samples.heights[0])
            {
                fFlat += h;
            }
            fFlat /= samples.heights[0].size();
            for (int i = 1; i < 3; i++)
            {
                for (float h : samples.heights[i])
                {
                    float d = h - fFlat;
                    nFrames[i]++;
                    fSum[i] += d;
                    fMin[i] = std::min(fMin[i], d);
                    fMax[i] = std::max(fMax[i], d);
                }
            }
        }
        for (int i = 1; i < 3; i++)
        {
            logger::info("stand debug: slopes %s %s: %d frames, height against the flat road mean %.3f min %.3f max %.3f",
                nClone ? "clones" : "ours", i == 1 ? "up" : "down", nFrames[i], nFrames[i] ? fSum[i] / nFrames[i] : 0.0f,
                nFrames[i] ? fMin[i] : 0.0f, nFrames[i] ? fMax[i] : 0.0f);
        }
    }
    ms_slopes.clear();
}
