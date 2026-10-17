#include "stdafx.h"
#include "CDebugCommands.h"
#include <fstream>
#include <sstream>
#include <CWanted.h>

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
