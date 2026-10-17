#include "stdafx.h"
#include "CDebugCommands.h"
#include <fstream>
#include <sstream>
#include <CWanted.h>
#include <CGangWars.h>
#include "Hooks/PlayerHooks.h"
#include "Hooks/WorldHooks.h"
#include <CDummyObject.h>

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
