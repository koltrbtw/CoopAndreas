#include "stdafx.h"
#include "CDebugCommands.h"
#include <fstream>
#include <sstream>
#include <CWanted.h>
#include <CGangWars.h>
#include "Hooks/PlayerHooks.h"
#include "Hooks/WorldHooks.h"
#include <CDummyObject.h>
#include <CTaskComplexEnterCarAsDriver.h>
#include <CCarEnterExit.h>
#include "CPassengerEnter.h"

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
        int n = sprintf_s(szState, "slot %d task %d in %d seat %d anims", nSlot, pTask ? pTask->GetTaskType() : -1,
            pPed->m_nPedFlags.bInVehicle, pPed->m_pVehicle && pPed->m_pVehicle->m_pDriver == pPed ? 0 : 1);
        for (auto pAssoc = RpAnimBlendClumpGetFirstAssociation(pPed->m_pRwClump); pAssoc && n < 240;
             pAssoc = RpAnimBlendGetNextAssociation(pAssoc))
        {
            if (pAssoc->m_fBlendAmount > 0.1f)
            {
                n += sprintf_s(szState + n, sizeof(szState) - n, " %d", pAssoc->m_nAnimId);
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
        if (!ms_watchedPed.empty() && to_string(pNetworkPed->m_pedId) == ms_watchedPed)
        {
            char szName[64];
            sprintf_s(szName, "watched %s (ours %d)", ms_watchedPed.c_str(), pNetworkPed->m_bSyncing);
            log(pNetworkPed->m_pPed, szName);
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
        plugin::Command<Commands::CREATE_CAR>(model, pos.x, pos.y, pos.z, &car);
        // a parked car with a driver, as a jacking needs
        int driver = 0;
        if (driverless != "empty")
        {
            plugin::Command<Commands::CREATE_RANDOM_CHAR_AS_DRIVER>(car, &driver);
            plugin::Command<Commands::CAR_SET_IDLE>(car);
        }
        logger::info("stand debug: car %d with driver %d at %.2f %.2f %.2f", car, driver, pos.x, pos.y, pos.z);
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
    else if (command == "watch-ped")
    {
        stream >> ms_watchedPed;
    }
    else if (command == "g")
    {
        ms_bPressG = true;
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

    // after CGame::Process (its CPad::UpdatePads would overwrite it), before CPassengerEnter::Process reads it
    if (ms_bPressG)
    {
        ms_bPressG = false;
        CPad* pPad = CPad::GetPad(0);
        pPad->OldState.DPadUp = 0;
        pPad->NewState.DPadUp = 255;
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
