#include "stdafx.h"
#include "CTrainSync.h"
#include "CPopulationHandover.h"
#include "CWorldGridSync.h"

// the carriage models of each train type, zero terminated (CTrain::CreateMissionTrain reads them from here)
static constexpr int MAX_CARRIAGES = 15;
static int* GetTrainConfig(int type)
{
    return reinterpret_cast<int*>(0x8D44F8) + type * (MAX_CARRIAGES + 1);
}

// DoTrainGenerationAndRemoval removes a random train once every carriage is this far from the camera (0x863994)
static constexpr float TRAIN_REMOVAL_DISTANCE = 220.0f;

// a clone further off than this from the syncer's track position jumps there instead of catching up
static constexpr float TRAIN_SNAP_DISTANCE = 30.0f;

static void SetMissionTrain(CTrain* pFrontCarriage, bool bMission)
{
    for (CTrain* pCarriage = pFrontCarriage; pCarriage; pCarriage = pCarriage->m_pNextCarriage)
    {
        pCarriage->m_nTrainFlags.bMissionTrain = bMission;
    }
}

int CTrainSync::FindTrainType(CTrain* pFrontCarriage)
{
    int aModels[MAX_CARRIAGES + 1]{};
    int nCarriages = 0;
    for (CTrain* pCarriage = pFrontCarriage; pCarriage; pCarriage = pCarriage->m_pNextCarriage)
    {
        if (nCarriages == MAX_CARRIAGES)
        {
            return -1;
        }
        aModels[nCarriages++] = pCarriage->m_nModelIndex;
    }

    // the type isn't stored anywhere after the creation; the carriage models tell it (the types with the same models
    // are the same train)
    for (int type = 0; type < TrainState::TRAIN_TYPE_COUNT; type++)
    {
        if (std::equal(aModels, aModels + nCarriages + 1, GetTrainConfig(type)))
        {
            return type;
        }
    }
    return -1;
}

void CTrainSync::CollectState(CNetworkVehicle* pNetworkVehicle, TrainState& state)
{
    auto pTrain = static_cast<CTrain*>(pNetworkVehicle->m_pVehicle);

    state.type = pNetworkVehicle->m_trainState.type;
    state.clockwise = pTrain->m_nTrainFlags.bClockwiseDirection;
    state.trackId = std::clamp<int8_t>(pTrain->m_nTrackId, 0, TrainState::TRACK_COUNT - 1);
    state.nodeIndex = std::clamp<int32_t>(pTrain->m_nNodeIndex, 0, 0x7FFF);
    state.railDistance = pTrain->m_fCurrentRailDistance;
    state.speed = pTrain->m_fTrainSpeed;
    state.cruiseSpeed = static_cast<uint8_t>(std::max<char>(pTrain->m_autoPilot.m_nCruiseSpeed, 0));
    state.stopsAtStations = pTrain->m_nTrainFlags.bStopsAtStations;
    state.derailed = pTrain->m_nTrainFlags.bNotOnARailRoad;
}

void CTrainSync::RegisterHosted(CTrain* pFrontCarriage)
{
    if (CNetworkVehicleManager::GetVehicle(pFrontCarriage))
    {
        return;
    }

    int type = FindTrainType(pFrontCarriage);
    if (type < 0)
    {
        logger::warn("train of model %d: unknown carriages, not synced", pFrontCarriage->m_nModelIndex);
        return;
    }

    auto pNetworkVehicle = new CNetworkVehicle();
    pNetworkVehicle->m_pVehicle = pFrontCarriage;
    pFrontCarriage->RegisterReference(reinterpret_cast<CEntity**>(&pNetworkVehicle->m_pVehicle));
    pNetworkVehicle->m_vehicleId = CNetworkVehicleManager::AllocateVehicleId();
    pNetworkVehicle->m_bSyncing = true;
    pNetworkVehicle->m_nModelId = pFrontCarriage->m_nModelIndex;
    pNetworkVehicle->m_nCreatedBy = pFrontCarriage->m_nTrainFlags.bMissionTrain ? MISSION_VEHICLE : RANDOM_VEHICLE;
    pNetworkVehicle->m_nOwnershipChangeTime = CTimer::m_snTimeInMilliseconds;
    pNetworkVehicle->m_bTrain = true;
    pNetworkVehicle->m_trainState.type = static_cast<uint8_t>(type);
    CollectState(pNetworkVehicle, pNetworkVehicle->m_trainState);
    CNetworkVehicleManager::Add(pNetworkVehicle);

    Packets::Vehicles::TrainSpawn packet{};
    packet.vehicleid = pNetworkVehicle->m_vehicleId;
    packet.pos = pFrontCarriage->GetPosition();
    packet.isMission = pNetworkVehicle->m_nCreatedBy == MISSION_VEHICLE;
    packet.state = pNetworkVehicle->m_trainState;
    GetPacketFactory().Send(packet);

    logger::info("train %s created: type %d track %d node %d mission %d", to_string(packet.vehicleid).c_str(), type,
        packet.state.trackId, packet.state.nodeIndex, packet.isMission);
}

void CTrainSync::KeepClone(CNetworkVehicle* pNetworkVehicle)
{
    auto pTrain = static_cast<CTrain*>(pNetworkVehicle->m_pVehicle);

    // again and again: a mission cleanup of our own scripts (CTrain::ReleaseMissionTrains) would give it to the
    // local generator, which removes random trains far from our camera
    SetMissionTrain(pTrain, true);

    // the syncer decides where it stops, the clone only follows its speed
    pTrain->m_nTrainFlags.bStopsAtStations = false;
}

void CTrainSync::Derail(CTrain* pFrontCarriage)
{
    // what CTrain::ProcessControl does at 0x6F8DE5 when a player drives too fast through a curve: every carriage
    // leaves the rails, collides and stops being static
    for (CTrain* pCarriage = pFrontCarriage; pCarriage; pCarriage = pCarriage->m_pNextCarriage)
    {
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(pCarriage) + 0x40) &= 0xFFFDFFFB;
        pCarriage->m_nTrainFlags.bNotOnARailRoad = true;
        pCarriage->SetIsStatic(false);
    }
}

void CTrainSync::Process()
{
    // the generator and the scripts go through CreateMissionTrain_Hook; this catches the rest (a train that was
    // there before we connected)
    for (auto pVehicle : CPools::ms_pVehiclePool)
    {
        if (CUtil::GetVehicleType(pVehicle) == VEHICLE_TRAIN &&
            static_cast<CTrain*>(pVehicle)->m_nTrainFlags.bIsFrontCarriage && !CNetworkVehicleManager::GetVehicle(pVehicle))
        {
            RegisterHosted(static_cast<CTrain*>(pVehicle));
        }
    }

    for (auto pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (!pNetworkVehicle->m_bTrain || !pNetworkVehicle->m_pVehicle)
        {
            continue;
        }

        if (!pNetworkVehicle->m_bSyncing)
        {
            KeepClone(pNetworkVehicle);
            continue;
        }

        CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;

        Packets::Vehicles::TrainUpdate packet{};
        packet.vehicleid = pNetworkVehicle->m_vehicleId;
        packet.pos = pVehicle->GetPosition();
        CollectState(pNetworkVehicle, packet.state);
        if (packet.state.derailed)
        {
            for (auto pCarriage = static_cast<CTrain*>(pVehicle);
                 pCarriage && packet.carriageCount < Packets::Vehicles::TrainUpdate::MAX_CARRIAGES;
                 pCarriage = pCarriage->m_pNextCarriage)
            {
                auto& carriage = packet.carriages[packet.carriageCount++];
                carriage.pos = pCarriage->GetPosition();
                carriage.rot = pCarriage->m_matrix->up;
                carriage.roll = pCarriage->m_matrix->right;
                carriage.velocity = pCarriage->m_vecMoveSpeed;
            }
        }
        pNetworkVehicle->m_trainState = packet.state;
        GetPacketFactory().Send(packet);
    }
}

void CTrainSync::HandleSpawn(Packets::Vehicles::TrainSpawn* pPacket)
{
    // the server sends a spawn whenever the train comes in our scope, never twice on purpose
    if (CNetworkVehicleManager::GetVehicle(pPacket->vehicleid))
    {
        return;
    }

    const TrainState& state = pPacket->state;
    if (static_cast<unsigned>(state.nodeIndex) >= NumTrackNodes[state.trackId])
    {
        logger::warn("train %s: node %d is not on track %d", to_string(pPacket->vehicleid).c_str(), state.nodeIndex,
            state.trackId);
        return;
    }

    // CreateMissionTrain expects the models loaded, like the generator and the scripts do before calling it
    int* pModels = GetTrainConfig(state.type);
    bool abWasRequired[MAX_CARRIAGES]{};
    for (int i = 0; pModels[i]; i++)
    {
        abWasRequired[i] = CStreaming::ms_aInfoForModel[pModels[i]].m_nFlags & GAME_REQUIRED;
        CStreaming::RequestModel(pModels[i], GAME_REQUIRED);
    }
    CStreaming::LoadAllRequestedModels(false);

    // a mission train: the front carriage is put at the position (FindPositionOnTrackFromCoors), and the local
    // generator leaves it alone
    CTrain* pFrontCarriage = nullptr;
    CTrain* pLastCarriage = nullptr;
    ms_bCreatingTrain = true;
    CTrain::CreateMissionTrain(pPacket->pos, state.clockwise, state.type, &pFrontCarriage, &pLastCarriage,
        state.nodeIndex, state.trackId, true);
    ms_bCreatingTrain = false;

    for (int i = 0; pModels[i]; i++)
    {
        if (!abWasRequired[i])
        {
            CStreaming::SetModelIsDeletable(pModels[i]);
            CStreaming::SetModelTxdIsDeletable(pModels[i]);
        }
    }

    if (!pFrontCarriage)
    {
        return;
    }

    // exactly the syncer's track position, the coordinates only found the nearest point
    pFrontCarriage->m_fCurrentRailDistance = state.railDistance;
    pFrontCarriage->m_nNodeIndex = static_cast<short>(state.nodeIndex);
    pFrontCarriage->m_fTrainSpeed = state.speed;

    auto pNetworkVehicle = new CNetworkVehicle();
    pNetworkVehicle->m_pVehicle = pFrontCarriage;
    pFrontCarriage->RegisterReference(reinterpret_cast<CEntity**>(&pNetworkVehicle->m_pVehicle));
    pNetworkVehicle->m_vehicleId = pPacket->vehicleid;
    pNetworkVehicle->m_bSyncing = false;
    pNetworkVehicle->m_nModelId = pFrontCarriage->m_nModelIndex;
    pNetworkVehicle->m_nCreatedBy = pPacket->isMission ? MISSION_VEHICLE : RANDOM_VEHICLE;
    pNetworkVehicle->m_nOwnershipChangeTime = CTimer::m_snTimeInMilliseconds;
    pNetworkVehicle->m_bTrain = true;
    pNetworkVehicle->m_trainState = state;
    CNetworkVehicleManager::Add(pNetworkVehicle);

    KeepClone(pNetworkVehicle);
    if (state.derailed)
    {
        Derail(pFrontCarriage);
    }

    logger::info("train %s spawned: type %d track %d node %d distance %.1f", to_string(pPacket->vehicleid).c_str(),
        state.type, state.trackId, state.nodeIndex, state.railDistance);
}

void CTrainSync::HandleUpdate(Packets::Vehicles::TrainUpdate* pPacket)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPacket->vehicleid);
    if (!pNetworkVehicle || !pNetworkVehicle->m_bTrain || pNetworkVehicle->m_bSyncing || !pNetworkVehicle->m_pVehicle)
    {
        return;
    }

    auto pTrain = static_cast<CTrain*>(pNetworkVehicle->m_pVehicle);
    const TrainState& state = pPacket->state;
    pNetworkVehicle->m_trainState = state;

    if (state.derailed)
    {
        if (!pTrain->m_nTrainFlags.bNotOnARailRoad)
        {
            Derail(pTrain);
            logger::info("train %s derailed", to_string(pPacket->vehicleid).c_str());
        }

        // the same carriages in the same order, both built from the train type
        CTrain* pCarriage = pTrain;
        for (int i = 0; i < pPacket->carriageCount && pCarriage; i++, pCarriage = pCarriage->m_pNextCarriage)
        {
            const auto& carriage = pPacket->carriages[i];
            pCarriage->m_matrix->pos = carriage.pos;
            pCarriage->m_matrix->up = carriage.rot;
            pCarriage->m_matrix->right = carriage.roll;
            pCarriage->m_vecMoveSpeed = carriage.velocity;
        }
        return;
    }

    if (pTrain->m_nTrainFlags.bNotOnARailRoad)
    {
        return;
    }

    // catch up instead of a jump each update; a big difference (also the wrap at the track end) is a jump
    float fError = state.railDistance - pTrain->m_fCurrentRailDistance;
    pTrain->m_fCurrentRailDistance =
        std::fabs(fError) > TRAIN_SNAP_DISTANCE ? state.railDistance : pTrain->m_fCurrentRailDistance + fError * 0.5f;
    pTrain->m_fTrainSpeed = state.speed;

    // between the updates the front carriage drives itself towards the cruise speed (CTrain::ProcessControl): make
    // it the syncer's speed (m_fTrainSpeed is per time step, the cruise speed 50 times that), and no stop at stations
    pTrain->m_autoPilot.m_nCruiseSpeed = static_cast<char>(std::min(std::fabs(state.speed) * 50.0f + 0.5f, 127.0f));
    pTrain->m_nTimeWhenStoppedAtStation = 0;
}

void CTrainSync::OnBecomeSyncer(CNetworkVehicle* pNetworkVehicle)
{
    auto pTrain = static_cast<CTrain*>(pNetworkVehicle->m_pVehicle);
    const TrainState& state = pNetworkVehicle->m_trainState;

    // an ambient train is ours to remove now, see HandOverDistantTrains
    SetMissionTrain(pTrain, pNetworkVehicle->m_nCreatedBy == MISSION_VEHICLE);
    pTrain->m_nTrainFlags.bStopsAtStations = state.stopsAtStations;
    if (state.cruiseSpeed)
    {
        pTrain->m_autoPilot.m_nCruiseSpeed = static_cast<char>(state.cruiseSpeed);
    }

    logger::info("train %s taken over", to_string(pNetworkVehicle->m_vehicleId).c_str());
}

void CTrainSync::OnBecomeClone(CNetworkVehicle* pNetworkVehicle)
{
    if (!pNetworkVehicle->m_pVehicle)
    {
        return;
    }

    // remembered for the time it comes back to us
    CollectState(pNetworkVehicle, pNetworkVehicle->m_trainState);
    KeepClone(pNetworkVehicle);
}

void CTrainSync::RemoveCloneTrain(CTrain* pFrontCarriage)
{
    // not under the local player: as a random train the local generator removes it once it is far away
    CPlayerPed* pLocalPlayer = FindPlayerPed(0);
    for (CTrain* pCarriage = pFrontCarriage; pCarriage; pCarriage = pCarriage->m_pNextCarriage)
    {
        if (pLocalPlayer && pLocalPlayer->m_pVehicle == pCarriage)
        {
            CTrain::ReleaseOneMissionTrain(pFrontCarriage);
            return;
        }
    }

    for (CTrain* pCarriage = pFrontCarriage; pCarriage; pCarriage = pCarriage->m_pNextCarriage)
    {
        CNetworkVehicle::TakeOutNetworkPlayers(pCarriage);
    }

    CTrain::RemoveOneMissionTrain(pFrontCarriage);
}

static void __cdecl CTrain__CreateMissionTrain_Hook(CVector posn, bool clockwiseDirection, unsigned int trainType,
    CTrain** ppOutFirstCarriage, CTrain** ppOutLastCarriage, int nodeIndex, int trackId, bool isMissionTrain)
{
    CTrain* pFrontCarriage = nullptr;

    CTrainSync::ms_bCreatingTrain = true;
    CTrain::CreateMissionTrain(posn, clockwiseDirection, trainType, &pFrontCarriage, ppOutLastCarriage, nodeIndex,
        trackId, isMissionTrain);
    CTrainSync::ms_bCreatingTrain = false;

    if (ppOutFirstCarriage)
    {
        *ppOutFirstCarriage = pFrontCarriage;
    }

    if (pFrontCarriage && CNetwork::m_bAuthenticated)
    {
        CTrainSync::RegisterHosted(pFrontCarriage);  // right away, a script may use it in the next command
    }
}

// whether the train will be removed by the generator this frame (0x6F79BE)
static bool IsRemovedByGenerator(CTrain* pFrontCarriage)
{
    CVehicle* pPlayerVehicle = FindPlayerVehicle(-1, false);
    const CVector& cameraPos = TheCamera.GetPosition();

    for (CTrain* pCarriage = pFrontCarriage; pCarriage; pCarriage = pCarriage->m_pNextCarriage)
    {
        const CVector& pos = pCarriage->GetPosition();
        float dx = pos.x - cameraPos.x;
        float dy = pos.y - cameraPos.y;
        if (pCarriage == pPlayerVehicle || dx * dx + dy * dy < TRAIN_REMOVAL_DISTANCE * TRAIN_REMOVAL_DISTANCE)
        {
            return false;
        }
    }
    return true;
}

// a random train is generated only where we spawn the population, and not when someone else's random train is
// here already (alone the game keeps one at a time too)
static bool MayGenerateTrain()
{
    for (auto pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (pNetworkVehicle->m_bTrain && !pNetworkVehicle->m_bSyncing && pNetworkVehicle->m_pVehicle &&
            pNetworkVehicle->m_nCreatedBy != MISSION_VEHICLE)
        {
            return false;
        }
    }

    // status 1: the models are loading and the generation node is chosen, the train comes at the node
    if (CTrain::GenTrain_Status == 1)
    {
        const CTrainNode& node = pTrackNodes[CTrain::GenTrain_Track][CTrain::GenTrain_GenerationNode];
        if (!CWorldGridSync::CanSpawnAt(CVector(node.x * 0.125f, node.y * 0.125f, node.z * 0.125f)))
        {
            // what the generator does when it gives up on a node (0x6F7D29): release the models, choose again
            for (int* pModel = GetTrainConfig(CTrain::GenTrain_TrainConfig); *pModel; pModel++)
            {
                CStreaming::SetModelIsDeletable(*pModel);
                CStreaming::SetModelTxdIsDeletable(*pModel);
            }
            CTrain::GenTrain_Status = 0;
            return false;
        }
    }
    return true;
}

static void __cdecl CTrain__DoTrainGenerationAndRemoval_Hook()
{
    if (!CNetwork::m_bAuthenticated)
    {
        CTrain::DoTrainGenerationAndRemoval();
        return;
    }

    // our random train about to be removed far from our camera: to a player that still needs it
    for (auto pNetworkVehicle : std::vector<CNetworkVehicle*>(CNetworkVehicleManager::m_pVehicles))
    {
        if (pNetworkVehicle->m_bTrain && pNetworkVehicle->m_bSyncing && pNetworkVehicle->m_pVehicle &&
            pNetworkVehicle->m_nCreatedBy != MISSION_VEHICLE &&
            IsRemovedByGenerator(static_cast<CTrain*>(pNetworkVehicle->m_pVehicle)))
        {
            CPopulationHandover::TryHandOverTrain(pNetworkVehicle);
        }
    }

    bool bDisabled = CTrain::bDisableRandomTrains;
    if (!bDisabled && !MayGenerateTrain())
    {
        CTrain::bDisableRandomTrains = true;
    }

    CTrain::DoTrainGenerationAndRemoval();
    CTrain::bDisableRandomTrains = bDisabled;
}

// the script commands for all the trains (DELETE_MISSION_TRAINS, MARK_MISSION_TRAINS_AS_NO_LONGER_NEEDED,
// DELETE_ALL_TRAINS, the mission cleanup) would remove or release the clones too, mission trains for the local game:
// they look for CVehicle::m_nVehicleType == VEHICLE_TRAIN, so meanwhile the clones aren't trains
template <uintptr_t address>
static void __cdecl CTrain__AllTrains_Hook()
{
    std::vector<CTrain*> hidden;
    for (auto pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (!pNetworkVehicle->m_bTrain || pNetworkVehicle->m_bSyncing || !pNetworkVehicle->m_pVehicle)
        {
            continue;
        }

        for (auto pCarriage = static_cast<CTrain*>(pNetworkVehicle->m_pVehicle); pCarriage;
             pCarriage = pCarriage->m_pNextCarriage)
        {
            pCarriage->m_nVehicleType = VEHICLE_AUTOMOBILE;
            hidden.push_back(pCarriage);
        }
    }

    plugin::Call<address>();

    for (auto pCarriage : hidden)
    {
        pCarriage->m_nVehicleType = VEHICLE_TRAIN;
    }
}

void CTrainSync::InjectHooks()
{
    patch::RedirectCall(0x6F7C2C, CTrain__CreateMissionTrain_Hook);  // DoTrainGenerationAndRemoval
    patch::RedirectCall(0x497FE4, CTrain__CreateMissionTrain_Hook);  // the CREATE_MISSION_TRAIN script command
    patch::RedirectCall(0x43426D, CTrain__DoTrainGenerationAndRemoval_Hook);  // CGame::Process

    patch::RedirectCall(0x498001, CTrain__AllTrains_Hook<0x6F6A20>);  // DELETE_MISSION_TRAINS: RemoveMissionTrains
    patch::RedirectCall(0x49800D, CTrain__AllTrains_Hook<0x6F6B60>);  // MARK_..._NO_LONGER_NEEDED: ReleaseMissionTrains
    patch::RedirectCall(0x4685AD, CTrain__AllTrains_Hook<0x6F6B60>);  // the mission cleanup: ReleaseMissionTrains
    patch::RedirectCall(0x498019, CTrain__AllTrains_Hook<0x6F6AA0>);  // DELETE_ALL_TRAINS: RemoveAllTrains
}
