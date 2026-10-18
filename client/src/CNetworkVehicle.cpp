#include "stdafx.h"
#include <CServerTime.h>
#include "CNetworkVehicle.h"
#include "CTrainSync.h"
#include <CTaskComplexEnterCar.h>

CNetworkVehicle::CNetworkVehicle(vehicleid_t vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2, unsigned char createdBy, const int8_t extras[2])
{
    if (auto vehicle = CNetworkVehicleManager::GetVehicle(vehicleid))
    {
        if (vehicle->m_pVehicle && vehicle->m_pVehicle->IsVTableValid())
        {
            CWorld::Remove(vehicle->m_pVehicle);
            delete vehicle->m_pVehicle;
        }
        CNetworkVehicleManager::Remove(vehicle);
    }

    m_vehicleId = vehicleid;
    m_bSyncing = false;
    OnSyncerPacket();
    m_nModelId = modelid;
    m_nCreatedBy = createdBy;

    if (!CreateVehicle(vehicleid, modelid, pos, rotation, color1, color2, extras))
        return;

}

bool CNetworkVehicle::CreateVehicle(vehicleid_t vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2, const int8_t extras[2])
{
    unsigned char oldFlags = CStreaming::ms_aInfoForModel[modelid].m_nFlags;
    CStreaming::RequestModel(modelid, GAME_REQUIRED);
    CStreaming::LoadAllRequestedModels(false);

    if (!(oldFlags & GAME_REQUIRED))
    {
        CStreaming::SetModelIsDeletable(modelid);
        CStreaming::SetModelTxdIsDeletable(modelid);
    }

    // the streaming busy (a mission start loads a lot at once) can leave it unloaded: the vehicle ctor then read the
    // model's missing clump (0x4C9691 from CVehicle::SetModelIndex, a BMX of a remote player). No vehicle here, as with
    // a full pool; the syncer's next packets bring it back
    if (CStreaming::ms_aInfoForModel[modelid].m_nLoadState != LOADSTATE_LOADED)
    {
        logger::info("vehicle %s: model %d not loaded, not created", to_string(vehicleid).c_str(), modelid);
        return false;
    }

    // the creator's extras instead of random ones: ChooseComponent/ChooseSecondComponent take ms_compsToUse
    // unless it's -2 and reset it, the way CCarGenerator sets them
    CVehicleModelInfo::ms_compsToUse[0] = extras[0];
    CVehicleModelInfo::ms_compsToUse[1] = extras[1];

    switch (((CVehicleModelInfo*)CModelInfo::ms_modelInfoPtrs[modelid])->m_nVehicleType)
    {
    case VEHICLE_MTRUCK:
        m_pVehicle = new CMonsterTruck(modelid, MISSION_VEHICLE); break;

    case VEHICLE_QUAD:
        m_pVehicle = new CQuadBike(modelid, MISSION_VEHICLE); break;

    case VEHICLE_HELI:
        m_pVehicle = new CHeli(modelid, MISSION_VEHICLE); break;

    case VEHICLE_PLANE:
        m_pVehicle = new CPlane(modelid, MISSION_VEHICLE); break;

    // the pool operator new gives nullptr when the vehicle pool is full
    case VEHICLE_BIKE:
        if (auto pBike = new CBike(modelid, MISSION_VEHICLE))
        {
            pBike->m_nDamageFlags |= 0x10;
            m_pVehicle = pBike;
        }
        break;

    case VEHICLE_BMX:
        if (auto pBmx = new CBmx(modelid, MISSION_VEHICLE))
        {
            pBmx->m_nDamageFlags |= 0x10;
            m_pVehicle = pBmx;
        }
        break;

    case VEHICLE_TRAILER:
        m_pVehicle = new CTrailer(modelid, MISSION_VEHICLE); break;

    case VEHICLE_BOAT:
        m_pVehicle = new CBoat(modelid, MISSION_VEHICLE); break;

    case VEHICLE_TRAIN:
        return false;

    default:
        m_pVehicle = new CAutomobile(modelid, MISSION_VEHICLE, true); break;
    }

    // not taken when the pool was full, it would go to the next vehicle the game creates
    CVehicleModelInfo::ms_compsToUse[0] = -2;
    CVehicleModelInfo::ms_compsToUse[1] = -2;

    if (!m_pVehicle)
    {
        logger::warn("vehicle %s of model %d: no room in the vehicle pool (%d used)", to_string(vehicleid).c_str(),
            modelid, CPools::ms_pVehiclePool->GetNoOfUsedSpaces());
        return false;
    }

    m_pVehicle->RegisterReference((CEntity**)&m_pVehicle);

    m_pVehicle->SetPosn(pos);
    m_pVehicle->SetOrientation(0.0f, 0.0f, rotation);
    m_pVehicle->m_nStatus = 4;
    m_pVehicle->m_eDoorLock = DOORLOCK_UNLOCKED;
    m_pVehicle->m_nPrimaryColor = color1;
    m_pVehicle->m_nSecondaryColor = color2;
    CWorld::Add(m_pVehicle);

    return true;
}

// a clone deleted under an enter task into it (our copy of an npc's or a player's, our own): the open door task went
// on with the vehicle reference cleared, the event it raises (0x6568AB) took the null vehicle - a crash in
// CEntity::RegisterReference (0x571B73) on the clone of that event (0x4AE9D3)
static void AbortEnterTasksInto(CVehicle* pVehicle)
{
    auto abort = [pVehicle](CPed* pPed)
    {
        if (!pPed || pPed->m_nPedFlags.bInVehicle)
        {
            return;
        }
        for (int taskType : {TASK_COMPLEX_ENTER_CAR_AS_PASSENGER, TASK_COMPLEX_ENTER_CAR_AS_DRIVER})
        {
            auto pTask = static_cast<CTaskComplexEnterCar*>(pPed->m_pIntelligence->m_TaskMgr.FindActiveTaskByType(taskType));
            if (pTask && pTask->m_pTargetVehicle == pVehicle)
            {
                pPed->m_pIntelligence->FlushImmediately(false);
                return;
            }
        }
    };

    abort(FindPlayerPed(0));
    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        abort(pNetworkPlayer->m_pPed);
    }
    for (auto pNetworkPed : CNetworkPedManager::m_pPeds)
    {
        abort(pNetworkPed->m_pPed);
    }
}

CNetworkVehicle::~CNetworkVehicle()
{
    if (m_pVehicle)
    {
        m_pVehicle->CleanUpOldReference((CEntity**)&m_pVehicle);
    }

    if (m_bSyncing)
    {
        Packets::Vehicles::VehicleRemove vehicleRemovePacket{};
        vehicleRemovePacket.vehicleid = m_vehicleId;
        GetPacketFactory().Send(vehicleRemovePacket);
    }
    else if (m_bTrain)
    {
        if (m_pVehicle && m_pVehicle->IsVTableValid())
        {
            CTrainSync::RemoveCloneTrain(static_cast<CTrain*>(m_pVehicle));
        }
    }
    else
    {
        if (m_pVehicle && m_pVehicle->IsVTableValid())
        {
            if (m_nBlipHandle != -1)
            {
                CRadar::ClearBlipForEntity(eBlipType::BLIP_CAR, CPools::GetVehicleRef(m_pVehicle));
            }
            AbortEnterTasksInto(m_pVehicle);
            TakeOutNetworkPlayers(m_pVehicle);
            CWorld::Remove(m_pVehicle);
            //CWorld::RemoveReferencesToDeletedObject(m_pVehicle); // ?
            delete m_pVehicle;
        }
    }
    m_pVehicle = nullptr;
}

void CNetworkVehicle::TakeOutNetworkPlayers(CVehicle* pVehicle)
{
    CPed* apOccupants[1 + 8] = {pVehicle->m_pDriver};
    std::copy(std::begin(pVehicle->m_apPassengers), std::end(pVehicle->m_apPassengers), apOccupants + 1);

    for (CPed* pPed : apOccupants)
    {
        if (CNetworkPlayer* pNetworkPlayer = pPed ? CNetworkPlayerManager::GetPlayer(pPed) : nullptr)
        {
            pNetworkPlayer->RemoveFromVehicle(pVehicle);
        }
    }
}

bool CNetworkVehicle::IsSeatHeldByPlayer(CVehicle* pVehicle, int seatid, CPed* pPed)
{
    if (seatid >= static_cast<int>(std::size(pVehicle->m_apPassengers)))
    {
        return false;
    }

    CPed* pOccupant = seatid < 0 ? pVehicle->m_pDriver : pVehicle->m_apPassengers[seatid];
    // by our records, not by the ped type: a remote player's CPlayerPed is created for a game slot past 1
    return pOccupant && pOccupant != pPed &&
           (pOccupant == FindPlayerPed(0) || CNetworkPlayerManager::GetPlayer(pOccupant) != nullptr);
}

bool CNetworkVehicle::HasDriver()
{
    if (m_pVehicle == nullptr)
        return false;

    return m_pVehicle->m_pDriver != nullptr;
}

CNetworkVehicle* CNetworkVehicle::CreateHosted(CVehicle* pVehicle)
{
    // an absolute time, keep the new vehicle for at least 5 seconds
    pVehicle->m_nTimeTillWeNeedThisCar =
        std::max<unsigned int>(pVehicle->m_nTimeTillWeNeedThisCar, CTimer::m_snTimeInMilliseconds + 5000);

    CNetworkVehicle* networkVehicle = new CNetworkVehicle();

    networkVehicle->m_pVehicle = pVehicle;
    pVehicle->RegisterReference((CEntity**)&networkVehicle->m_pVehicle);

    networkVehicle->m_vehicleId = CNetworkVehicleManager::AllocateVehicleId();
    networkVehicle->m_bSyncing = true;
    networkVehicle->m_nModelId = pVehicle->m_nModelIndex;
    networkVehicle->m_nPaintJob = (char)pVehicle->GetRemapIndex();
    networkVehicle->m_nCreatedBy = pVehicle->m_nCreatedBy;

    CNetworkVehicleManager::Add(networkVehicle);

    Packets::Vehicles::VehicleSpawn vehicleSpawnPacket{};
    vehicleSpawnPacket.vehicleid = networkVehicle->m_vehicleId;
    vehicleSpawnPacket.modelid = pVehicle->m_nModelIndex;
    vehicleSpawnPacket.pos = pVehicle->m_matrix->pos;
    vehicleSpawnPacket.rot = pVehicle->GetHeading();
    vehicleSpawnPacket.color1 = pVehicle->m_nPrimaryColor;
    vehicleSpawnPacket.color2 = pVehicle->m_nSecondaryColor;
    vehicleSpawnPacket.createdBy = (eVehicleCreatedBy)pVehicle->m_nCreatedBy;
    for (int i = 0; i < 2; i++)
    {
        // extra1..6 or none, CVehicle::SetModelIndex fills them from ChooseComponent
        int8_t nExtra = pVehicle->m_anExtras[i];
        vehicleSpawnPacket.extras[i] = nExtra >= -1 && nExtra <= 5 ? nExtra : -1;
    }
    GetPacketFactory().Send(vehicleSpawnPacket);

    return networkVehicle;
}

void CNetworkVehicle::BecomeSyncer(const Packets::Vehicles::VehicleHandoff* pHandoff)
{
    m_bSyncing = true;
    m_nOwnershipChangeTime = CTimer::m_snTimeInMilliseconds;
    // a mission vehicle we got (by the collision rule of CPopulationHandover::Process, or we drove it) goes back to
    // the host, whose scripts drive it (SET_CAR_HEALTH, LOCK_CAR_DOORS, CAR_GOTO...), once nobody is inside
    m_bBorrowed = m_nCreatedBy == MISSION_VEHICLE && !CLocalPlayer::m_bIsHost;
    m_bHasTarget = false;
    m_bHasRotorSpeed = false;
    m_bKeepSpeed = false;
    m_bHasGear = false;
    m_nLastIdleUpdateTime = 0;

    if (!m_pVehicle)
    {
        return;
    }

    if (m_bTrain)
    {
        CTrainSync::OnBecomeSyncer(this);
        return;
    }

    m_pVehicle->SetVehicleCreatedBy(m_nCreatedBy);

    if (!pHandoff)
    {
        return;
    }

    if (pHandoff->hasTransform)
    {
        // the whole matrix: a tilted one with the old up axis was skewed
        CMatrixLink* pMatrix = m_pVehicle->m_matrix;
        pMatrix->pos = pHandoff->pos;
        pMatrix->right = pHandoff->roll;
        pMatrix->up = pHandoff->rot;
        const CVector& r = pMatrix->right;
        const CVector& f = pMatrix->up;
        pMatrix->at = CVector(r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x);
        m_pVehicle->m_vecMoveSpeed = pHandoff->velocity;
        m_pVehicle->UpdateRwMatrix();
        m_pVehicle->UpdateRwFrame();
        m_pVehicle->RemoveAndAdd();
    }

    // the population code may remove it again, but not in the same frame
    m_pVehicle->m_nTimeTillWeNeedThisCar = CTimer::m_snTimeInMilliseconds + 5000;

    // keep them for the npc driver, whose handoff may come later (a leaving player's peds and vehicles go separately)
    m_autoPilotStyle = pHandoff->drivingStyle;
    m_autoPilotCruiseSpeed = pHandoff->cruiseSpeed;
    ResumeDriving();
}

void CNetworkVehicle::ResumeDriving()
{
    if (!m_bSyncing || !m_pVehicle || m_nCreatedBy == MISSION_VEHICLE)
    {
        return;
    }

    CPed* pDriver = m_pVehicle->m_pDriver;
    CNetworkPed* pNetworkDriver = pDriver ? CNetworkPedManager::GetPed(pDriver) : nullptr;
    if (!pNetworkDriver || !pNetworkDriver->m_bSyncing || pNetworkDriver->m_nCreatedBy != RANDOM_CHAR)
    {
        return;
    }

    // the same task the game gives to ambient drivers (CPopulation::AddPedInCar); its SetUpCar (0x63CB60) joins
    // the road network from the current position and sets the cruise mission, the clone autopilot is garbage
    auto pTask = new CTaskComplexCarDriveWander(
        m_pVehicle, m_autoPilotStyle, static_cast<float>(m_autoPilotCruiseSpeed ? m_autoPilotCruiseSpeed : 10));
    pDriver->m_pIntelligence->m_TaskMgr.SetTask(pTask, TASK_PRIMARY_PRIMARY, false);
}

void CNetworkVehicle::BecomeClone(Packets::Vehicles::VehicleHandoff* pHandoff)
{
    pHandoff->vehicleid = m_vehicleId;
    pHandoff->released = true;

    // the train driver is a local npc of everyone, nothing goes with it
    if (m_bTrain)
    {
        pHandoff->hasTransform = false;
        m_bSyncing = false;
        m_nOwnershipChangeTime = CTimer::m_snTimeInMilliseconds;
        CTrainSync::OnBecomeClone(this);
        return;
    }

    if (m_pVehicle)
    {
        pHandoff->pos = m_pVehicle->m_matrix->pos;
        pHandoff->rot = m_pVehicle->m_matrix->up;
        pHandoff->roll = m_pVehicle->m_matrix->right;
        pHandoff->velocity = m_pVehicle->m_vecMoveSpeed;
        pHandoff->carMission = m_pVehicle->m_autoPilot.m_nCarMission;
        pHandoff->drivingStyle = m_pVehicle->m_autoPilot.m_nCarDrivingStyle;
        pHandoff->cruiseSpeed = static_cast<uint8_t>(m_pVehicle->m_autoPilot.m_nCruiseSpeed);

        // our npc peds inside go together with the vehicle
        CPed* apOccupants[1 + 8] = {m_pVehicle->m_pDriver};
        std::copy(std::begin(m_pVehicle->m_apPassengers), std::end(m_pVehicle->m_apPassengers), apOccupants + 1);

        for (int i = 0; i < 1 + 8; i++)
        {
            CNetworkPed* pNetworkPed = apOccupants[i] ? CNetworkPedManager::GetPed(apOccupants[i]) : nullptr;
            if (!pNetworkPed || !pNetworkPed->m_bSyncing)
            {
                continue;
            }

            auto& occupant = pHandoff->occupants[pHandoff->occupantCount++];
            occupant.pedid = pNetworkPed->m_pedId;
            occupant.seatid = static_cast<int8_t>(i - 1);
            pNetworkPed->BecomeClone(true);
        }

        // otherwise after the vehicle comes back to us it would follow a stale route; the route of a mission
        // vehicle is the script's, it goes on with it when the vehicle comes back
        if (m_nCreatedBy != MISSION_VEHICLE)
        {
            m_pVehicle->m_autoPilot.m_nCarMission = MISSION_NONE;
            m_pVehicle->m_autoPilot.m_nTempAction = 0;
        }
    }

    m_bSyncing = false;
    m_bBorrowed = false;
    m_nOwnershipChangeTime = CTimer::m_snTimeInMilliseconds;
    OnSyncerPacket();

    if (m_pVehicle)
    {
        m_pVehicle->SetVehicleCreatedBy(MISSION_VEHICLE);
    }
}

vehicleid_t CNetworkVehicle::GetTrailerId(CVehicle* pVehicle)
{
    CNetworkVehicle* pNetworkTrailer = pVehicle->m_pTrailer ? CNetworkVehicleManager::GetVehicle(pVehicle->m_pTrailer) : nullptr;
    return pNetworkTrailer ? pNetworkTrailer->m_vehicleId : vehicleid_t();
}

void CNetworkVehicle::ApplyTrailer(vehicleid_t trailerid)
{
    if (!m_pVehicle || m_bSyncing)
    {
        return;
    }

    CNetworkVehicle* pNetworkTrailer = trailerid.valid() ? CNetworkVehicleManager::GetVehicle(trailerid) : nullptr;
    CVehicle* pWanted = pNetworkTrailer && !pNetworkTrailer->m_bSyncing ? pNetworkTrailer->m_pVehicle : nullptr;
    CVehicle* pCurrent = m_pVehicle->m_pTrailer;

    if (pCurrent == pWanted)
    {
        return;
    }

    if (pCurrent)
    {
        pCurrent->BreakTowLink();
    }

    // a clone is STATUS_ABANDONED, which CTrailer::SetTowLink accepts; put it at the tow bar right away
    if (pWanted)
    {
        pWanted->SetTowLink(m_pVehicle, true);
    }
}

bool CNetworkVehicle::CollectDamage(Packets::Vehicles::VehicleDamage* pPacket)
{
    if (!m_pVehicle)
    {
        return false;
    }

    // the classes built on CAutomobile (quads, monster trucks, helis, planes, trailers) keep this type too
    if (m_pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
    {
        CAutomobile* pAutomobile = static_cast<CAutomobile*>(m_pVehicle);
        if (m_oldDamageState == pAutomobile->m_damageManager)
        {
            return false;
        }
        m_oldDamageState = pAutomobile->m_damageManager;
        pPacket->damageManager = pAutomobile->m_damageManager;
    }
    else if (m_pVehicle->m_nVehicleType == VEHICLE_BIKE)
    {
        CBike* pBike = static_cast<CBike*>(m_pVehicle);
        if (memcmp(m_aOldBikeWheelStatus, pBike->m_anWheelDamageState, sizeof(m_aOldBikeWheelStatus)) == 0)
        {
            return false;
        }
        memcpy(m_aOldBikeWheelStatus, pBike->m_anWheelDamageState, sizeof(m_aOldBikeWheelStatus));
        memcpy(pPacket->bikeWheelStatus, pBike->m_anWheelDamageState, sizeof(m_aOldBikeWheelStatus));
    }
    else
    {
        return false;
    }

    pPacket->vehicleid = m_vehicleId;
    return true;
}

// how broken a part is, a door that only swings open is not
static int GetDoorDamageLevel(uint8_t status)
{
    if (status == DT_DOOR_MISSING)
    {
        return 2;
    }
    return status >= DT_DOOR_BASHED ? 1 : 0;
}

// something got less damaged: Pay 'n' Spray, a cheat, FIX_CAR of a mission
static bool IsRepaired(const CDamageManager& current, const CDamageManager& next)
{
    for (int i = 0; i < 4; i++)
    {
        if (next.m_anWheelsStatus[i] < current.m_anWheelsStatus[i])
        {
            return true;
        }
    }
    for (int i = 0; i < 6; i++)
    {
        if (GetDoorDamageLevel(next.m_anDoorsStatus[i]) < GetDoorDamageLevel(current.m_anDoorsStatus[i]))
        {
            return true;
        }
    }
    for (int i = 0; i < 4; i++)
    {
        if (((next.m_nLightsStatus >> (i * 2)) & 3) < ((current.m_nLightsStatus >> (i * 2)) & 3))
        {
            return true;
        }
    }
    for (int i = 0; i < 7; i++)
    {
        if (((next.m_nPanelsStatus >> (i * 4)) & 0xF) < ((current.m_nPanelsStatus >> (i * 4)) & 0xF))
        {
            return true;
        }
    }
    return false;
}

static constexpr int PANEL_WINDSCREEN = 4;
static constexpr int PANEL_FRONT_BUMPER = 5;
static constexpr int PANEL_REAR_BUMPER = 6;
static constexpr int PANEL_STATE_MISSING = 3;

static int GetPanelStatus(const CDamageManager& damageManager, int panel)
{
    return (damageManager.m_nPanelsStatus >> (panel * 4)) & 0xF;
}

// one part at a time and only what changed: SetupDamageAfterLoad called every setter with the effects on, so each
// packet threw all the missing parts off the car again and slammed the bashed doors shut. bEffects: the part breaks
// off now (a flying part, the windscreen glass), not when the clone is created or after a repair
static void ApplyAutomobileDamage(CAutomobile* pAutomobile, const CDamageManager& next, bool bEffects)
{
    CDamageManager previous = pAutomobile->m_damageManager;
    pAutomobile->m_damageManager = next;

    for (int i = 0; i < 6; i++)
    {
        if (!bEffects || previous.m_anDoorsStatus[i] != next.m_anDoorsStatus[i])
        {
            pAutomobile->SetDoorDamage((eDoors)i, !bEffects);
        }
    }

    for (int panel : {PANEL_FRONT_BUMPER, PANEL_REAR_BUMPER})
    {
        if (!bEffects || GetPanelStatus(previous, panel) != GetPanelStatus(next, panel))
        {
            pAutomobile->SetBumperDamage((ePanels)panel, !bEffects);
        }
    }

    // the wings fly off with createWindowGlass false, the windscreen shatters with it true
    for (int panel : {0, 1, PANEL_WINDSCREEN})
    {
        if (bEffects && GetPanelStatus(previous, panel) == GetPanelStatus(next, panel))
        {
            continue;
        }

        if (panel == PANEL_WINDSCREEN && !bEffects && GetPanelStatus(next, panel) == PANEL_STATE_MISSING)
        {
            if (RwFrame* pFrame = pAutomobile->m_aCarNodes[CAR_WINDSCREEN])
            {
                pAutomobile->SetComponentVisibility(pFrame, 0);  // ATOMIC_NONE, what the setter does without the glass
            }
            continue;
        }
        pAutomobile->SetPanelDamage((ePanels)panel, panel == PANEL_WINDSCREEN || !bEffects);
    }
}

void CNetworkVehicle::ApplyDamage(const Packets::Vehicles::VehicleDamage& damage)
{
    if (!m_pVehicle || !m_pVehicle->IsVTableValid())
    {
        return;
    }

    if (m_pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
    {
        CAutomobile* pAutomobile = static_cast<CAutomobile*>(m_pVehicle);

        // the damage setters only break parts, they never put a fixed one back: start from a whole car
        bool bEffects = m_bDamageApplied;
        if (IsRepaired(pAutomobile->m_damageManager, damage.damageManager))
        {
            pAutomobile->Fix();
            bEffects = false;
        }

        ApplyAutomobileDamage(pAutomobile, damage.damageManager, bEffects);
        m_oldDamageState = pAutomobile->m_damageManager;
        m_bDamageApplied = true;
    }
    else if (m_pVehicle->m_nVehicleType == VEHICLE_BIKE)
    {
        CBike* pBike = static_cast<CBike*>(m_pVehicle);
        memcpy(pBike->m_anWheelDamageState, damage.bikeWheelStatus, sizeof(m_aOldBikeWheelStatus));
        memcpy(m_aOldBikeWheelStatus, damage.bikeWheelStatus, sizeof(m_aOldBikeWheelStatus));
    }
}

// the error correction of MTA (CClientVehicle::SetTargetPosition / UpdateTargetPosition): a part of the position
// error is added over the time to the next update, while the physics moves the clone with the syncer's speed. Setting
// the update's position outright pulled the clone back to where the syncer's vehicle was a moment ago, 10 cm a packet
// on average and up to 2 m: it stuttered at speed
static constexpr float TARGET_POS_FACTOR_MIN = 0.25f;  // of the error, for an update interval of 100 ms or less
static constexpr float TARGET_ROT_FACTOR_MIN = 0.4f;
static constexpr float TARGET_MAX_ALPHA = 1.5f;        // may overshoot by half, the clone lags behind anyway
static constexpr float WARP_DISTANCE = 15.0f;          // a teleport, per 100 ms of the interval
static constexpr float WARP_DISTANCE_FOR_SPEED = 10.0f;
static constexpr unsigned int MIN_TARGET_DELAY = 50;
static constexpr unsigned int MAX_TARGET_DELAY = 400;
static constexpr int MAX_EXTRAPOLATION = 150;  // ms, MTA's vehext_ping_limit

static float GetErrorFactor(float fMin, unsigned int nDelay)
{
    float t = std::clamp((static_cast<float>(nDelay) - 100.0f) / 300.0f, 0.0f, 1.0f);
    return fMin + (1.0f - fMin) * t;
}

// the world keeps a boat as it was added, MTA's FixBoatOrientation does this after every SetMatrix
static void UpdateVehicleMatrix(CVehicle* pVehicle, bool bRotated)
{
    if (bRotated && pVehicle->m_nVehicleType == VEHICLE_BOAT)
    {
        CWorld::Remove(pVehicle);
        pVehicle->UpdateRwMatrix();
        pVehicle->UpdateRwFrame();
        CWorld::Add(pVehicle);
        return;
    }
    pVehicle->UpdateRwMatrix();
    pVehicle->UpdateRwFrame();
}

static void SetVehicleAxes(CVehicle* pVehicle, CVector right, CVector forward)
{
    right.Normalise();
    float fDot = forward.x * right.x + forward.y * right.y + forward.z * right.z;
    forward = forward - right * fDot;
    forward.Normalise();
    pVehicle->m_matrix->right = right;
    pVehicle->m_matrix->up = forward;  // plugin-sdk: up is the forward axis, at the up one
    CVector up;
    up.Cross(right, forward);
    pVehicle->m_matrix->at = up;
}

void CNetworkVehicle::SetTargetTransform(const CVector& pos, const CVector& right, const CVector& forward,
    const CVector& moveSpeed, const CVector& turnSpeed, server_time_t sendTime)
{
    if (!m_pVehicle || !m_pVehicle->m_matrix)
    {
        return;
    }

    m_pVehicle->m_vecMoveSpeed = moveSpeed;
    m_pVehicle->m_vecTurnSpeed = turnSpeed;

    unsigned int nNow = CTimer::m_snTimeInMilliseconds;
    unsigned int nDelay = std::clamp(m_nLastTargetTime ? nNow - m_nLastTargetTime : MIN_TARGET_DELAY, MIN_TARGET_DELAY,
        MAX_TARGET_DELAY);
    m_nLastTargetTime = nNow;

    // a towed trailer follows its tractor, the interpolation would only fight the tow link
    if (m_pVehicle->m_pTractor)
    {
        m_bHasTarget = false;
        return;
    }

    CVector current = m_pVehicle->GetPosition();

    // stuck in the ground here while the syncer's one stands above it (MTA's UpdateUnderFloorFix): two updates in a
    // row and it goes up at once
    bool bAircraft = m_pVehicle->m_nVehicleSubType == VEHICLE_HELI || m_pVehicle->m_nVehicleSubType == VEHICLE_PLANE;
    float fUnder = pos.z - current.z;
    if (!bAircraft && fUnder > 0.4f && fUnder < 10.0f && std::fabs(moveSpeed.z) < 0.01f)
    {
        if (++m_nUnderFloorCount >= 2)
        {
            current.z = pos.z;
            m_pVehicle->m_matrix->pos.z = pos.z;
            m_nUnderFloorCount = 0;
        }
    }
    else
    {
        m_nUnderFloorCount = 0;
    }

    float fWarp = (WARP_DISTANCE + WARP_DISTANCE_FOR_SPEED * CVector(moveSpeed).Magnitude()) * nDelay / 100.0f;
    if (!(DistanceBetweenPoints(current, pos) <= fWarp))  // NaN snaps too
    {
        m_pVehicle->m_matrix->pos = pos;
        SetVehicleAxes(m_pVehicle, right, forward);
        UpdateVehicleMatrix(m_pVehicle, true);
        m_pVehicle->RemoveAndAdd();
        m_bHasTarget = false;
        return;
    }

    // where the syncer's vehicle is by now (MTA's vehext extrapolation, by the measured delay instead of the ping):
    // the clone stayed behind it by its speed times the delay
    int nLatency = static_cast<int>(g_serverTime - sendTime);
    CVector target = pos;
    if (sendTime && nLatency > 0)
    {
        target += moveSpeed * (50.0f * std::min(nLatency, MAX_EXTRAPOLATION) / 1000.0f);  // the speed is per 1/50 s
    }

    m_vecTargetPosError = (target - current) * GetErrorFactor(TARGET_POS_FACTOR_MIN, nDelay);
    m_vecTargetRight = right;
    m_vecTargetForward = forward;
    m_fTargetRotFactor = GetErrorFactor(TARGET_ROT_FACTOR_MIN, nDelay);
    m_nTargetStart = nNow;
    m_nTargetFinish = nNow + nDelay;
    m_fTargetLastAlpha = 0.0f;
    m_bHasTarget = true;
}

void CNetworkVehicle::ProcessTargetTransform()
{
    if (!m_bHasTarget || !m_pVehicle || !m_pVehicle->m_matrix)
    {
        return;
    }

    float fAlpha = static_cast<float>(CTimer::m_snTimeInMilliseconds - m_nTargetStart) /
                   static_cast<float>(std::max(m_nTargetFinish - m_nTargetStart, 1u));
    fAlpha = std::clamp(fAlpha, 0.0f, TARGET_MAX_ALPHA);
    float fDelta = fAlpha - m_fTargetLastAlpha;

    // the rotation never overshoots: its share of the error goes in until alpha 1
    float fRotDelta = std::min(fAlpha, 1.0f) - std::min(m_fTargetLastAlpha, 1.0f);
    m_fTargetLastAlpha = fAlpha;
    if (fDelta <= 0.0f)
    {
        return;
    }

    m_pVehicle->m_matrix->pos += m_vecTargetPosError * fDelta;

    bool bRotated = false;
    if (fRotDelta > 0.0f)
    {
        float k = m_fTargetRotFactor * fRotDelta;
        CVector right = m_pVehicle->m_matrix->right;
        CVector forward = m_pVehicle->m_matrix->up;
        SetVehicleAxes(m_pVehicle, right + (m_vecTargetRight - right) * k, forward + (m_vecTargetForward - forward) * k);
        bRotated = true;
    }
    UpdateVehicleMatrix(m_pVehicle, bRotated);

    if (fAlpha >= TARGET_MAX_ALPHA)
    {
        m_bHasTarget = false;
    }
}

bool CNetworkVehicle::GetRotorSpeed(CVehicle* pVehicle, float* pfSpeed)
{
    switch (pVehicle->m_nVehicleSubType)
    {
    case VEHICLE_HELI:
        *pfSpeed = static_cast<CAutomobile*>(pVehicle)->m_fWheelSpeed[1];  // m_fHeliRotorSpeed (0x84C)
        return true;
    case VEHICLE_PLANE:
        *pfSpeed = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pVehicle) + 0x9C4);  // CPlane::m_fPropSpeed
        return true;
    case VEHICLE_BOAT:
        *pfSpeed = static_cast<CBoat*>(pVehicle)->m_fPropSpeed;
        return true;
    }
    return false;
}

void CNetworkVehicle::SetRotorSpeed(CVehicle* pVehicle, float fSpeed)
{
    switch (pVehicle->m_nVehicleSubType)
    {
    case VEHICLE_HELI:
        static_cast<CAutomobile*>(pVehicle)->m_fWheelSpeed[1] = fSpeed;
        break;
    case VEHICLE_PLANE:
        *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(pVehicle) + 0x9C4) = fSpeed;
        break;
    case VEHICLE_BOAT:
        static_cast<CBoat*>(pVehicle)->m_fPropSpeed = fSpeed;
        break;
    }
}

// CPlane::m_fLandingGearStatus: 0 down, 1 up, going up from 0 to 1, going down from -1 to 0. The clone got the
// syncer's 0 or 1 and snapped; now it starts the move like the player's key does in CPlane::ProcessControl
// (0x6CB39A, MTA's SetLandingGearDown) and moves it like 0x6CB422 when the game doesn't (no driver to process it)
void CNetworkVehicle::ProcessLandingGear()
{
    if (!m_bHasGear || !m_pVehicle || m_pVehicle->m_nVehicleSubType != VEHICLE_PLANE)
    {
        return;
    }

    CPlane* pPlane = static_cast<CPlane*>(m_pVehicle);
    float& fGear = pPlane->m_fLandingGearStatus;
    float fStep = CTimer::ms_fTimeStep * *reinterpret_cast<float*>(0x871904);
    auto SetWheelsAway = [pPlane](uint8_t nState) {
        memset(reinterpret_cast<uint8_t*>(pPlane) + 0x5A5, nState, 4);  // the wheel collision of the four wheels
    };

    bool bGoingUp = fGear > 0.0f && fGear < 1.0f;
    bool bGoingDown = fGear < 0.0f;
    if (m_bGearUp && fGear == 0.0f)
    {
        SetWheelsAway(2);
        fGear = fStep;
    }
    else if (!m_bGearUp && fGear == 1.0f)
    {
        fGear = fStep - 1.0f;
    }
    else if ((m_bGearUp && bGoingDown) || (!m_bGearUp && bGoingUp))
    {
        fGear = -fGear;
    }
    else if ((bGoingUp || bGoingDown) && fGear == m_fGearAfterLastFrame)
    {
        fGear += fStep;
        if (bGoingDown && fGear >= 0.0f)
        {
            plugin::CallMethod<0x6CAC70, CPlane*>(pPlane);  // CPlane::SetGearDown
        }
        else if (bGoingUp && fGear >= 1.0f)
        {
            plugin::CallMethod<0x6CAC20, CPlane*>(pPlane);  // CPlane::SetGearUp
        }
    }
    m_fGearAfterLastFrame = fGear;
}

static constexpr int DOOR_NODES[6] = {CAR_BONNET, CAR_BOOT, CAR_DOOR_LF, CAR_DOOR_RF, CAR_DOOR_LR, CAR_DOOR_RR};
// the bit of a door (eDoors) in CVehicle::m_nGettingInFlags / m_nGettingOutFlags, see CCarEnterExit::ComputeDoorFlag
static constexpr uint8_t DOOR_ENTER_EXIT_FLAGS[6] = {0, 0, 1, 4, 2, 8};
// the flags drop before the door is shut behind the ped (CTaskSimpleCarCloseDoorFromInside / Outside), the syncer's
// copy of the task finishes up to the ping later
static constexpr unsigned int DOOR_FREE_AFTER_ENTER_EXIT = 1500;
static constexpr unsigned int DOOR_BLEND_BACK = 400;
static constexpr uint8_t SIDE_DOORS = (1 << DOOR_FRONT_LEFT) | (1 << DOOR_FRONT_RIGHT) | (1 << DOOR_REAR_LEFT) | (1 << DOOR_REAR_RIGHT);

// the eDoors bits of a CCarEnterExit door id (10 driver, 8 front passenger, 11 / 9 rear left / right), 0 not yet chosen
static uint8_t GetDoorBit(int doorId)
{
    switch (doorId)
    {
    case 10: return 1 << DOOR_FRONT_LEFT;
    case 8: return 1 << DOOR_FRONT_RIGHT;
    case 11: return 1 << DOOR_REAR_LEFT;
    case 9: return 1 << DOOR_REAR_RIGHT;
    }
    return 0;
}

// the doors a player's enter task is going to open: the flags come only at the door, the walk there takes a while and
// the syncer's ratio (its copy of the task may be ahead) must not open the door before the get in anim does
static uint8_t GetDoorsOfEnterTask(CPed* pPed, CVehicle* pVehicle)
{
    if (pPed == nullptr)
    {
        return 0;
    }

    auto& taskManager = pPed->m_pIntelligence->m_TaskMgr;
    for (int taskType : {TASK_COMPLEX_ENTER_CAR_AS_PASSENGER, TASK_COMPLEX_ENTER_CAR_AS_DRIVER})
    {
        auto pTask = static_cast<CTaskComplexEnterCar*>(taskManager.FindActiveTaskByType(taskType));
        if (pTask && pTask->m_pTargetVehicle == pVehicle)
        {
            // a passenger's door is given from the start (CPassengerEnter), a driver's chosen at the car
            uint8_t nDoor = GetDoorBit(pTask->m_nTargetDoor) | GetDoorBit(pTask->m_nTargetSeat);
            return nDoor ? nDoor : SIDE_DOORS;
        }
    }
    return 0;
}
static constexpr uintptr_t OFFSET_TURRET = 0x94C;       // two floats, MTA's CVehicleSA::Get/SetTurretRotation
static constexpr uintptr_t OFFSET_SMOKE_TRAIL = 0xA00;  // CPlane, MTA's SetSmokeTrailEnabled
static constexpr uintptr_t OFFSET_SEARCH_LIGHT = 0xA11; // CHeli, see CHeli::PreRender

static bool HasTurret(CVehicle* pVehicle)
{
    switch (pVehicle->m_nModelIndex)
    {
    case MODEL_RHINO:
    case MODEL_FIRETRUK:
    case MODEL_SWATVAN:
        return true;
    }
    return false;
}

// CVehicle::m_nAlarmState: ms left of the ringing (ProcessCarAlarm, 0x6D21F0, counts it down), 0xFFFF armed and quiet
static constexpr unsigned short ALARM_STATE_ARMED = 0xFFFF;
static constexpr unsigned short ALARM_DURATION = 2000;  // ms, more than an idle keepalive: refreshed while it rings

template <typename T>
static T& VehicleField(CVehicle* pVehicle, uintptr_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(pVehicle) + offset);
}

void CNetworkVehicle::CollectExtras(CVehicle* pVehicle, SVehicleExtras& extras)
{
    extras.engineOn = pVehicle->m_nVehicleFlags.bEngineOn;
    extras.lightsOn = pVehicle->m_nVehicleFlags.bLightsOn;
    extras.heliSearchLight = pVehicle->m_nVehicleSubType == VEHICLE_HELI && VehicleField<bool>(pVehicle, OFFSET_SEARCH_LIGHT);
    extras.smokeTrail = pVehicle->m_nVehicleSubType == VEHICLE_PLANE && VehicleField<uint8_t>(pVehicle, OFFSET_SMOKE_TRAIL) == 1;
    extras.siren = pVehicle->m_nVehicleFlags.bSirenOrAlarm;
    extras.alarm = pVehicle->m_nAlarmState > 0 && pVehicle->m_nAlarmState != ALARM_STATE_ARMED;
    extras.overrideLights = static_cast<uint8_t>(std::min<unsigned int>(pVehicle->m_nOverrideLights, 2));

    if (pVehicle->m_nVehicleType != VEHICLE_AUTOMOBILE)
    {
        return;
    }

    CAutomobile* pAutomobile = static_cast<CAutomobile*>(pVehicle);
    for (int i = 0; i < 6; i++)
    {
        float fRatio = std::clamp(pAutomobile->m_doors[i].GetAngleOpenRatio(), 0.0f, 1.0f);
        extras.doors[i] = static_cast<uint8_t>(fRatio * 255.0f + 0.5f);
        extras.hasDoors |= extras.doors[i] != 0;
    }

    if (HasTurret(pVehicle))
    {
        extras.hasTurret = true;
        extras.turretHorizontal = VehicleField<float>(pVehicle, OFFSET_TURRET);
        extras.turretVertical = VehicleField<float>(pVehicle, OFFSET_TURRET + 4);
    }
}

void CNetworkVehicle::ApplyExtras(const SVehicleExtras& extras)
{
    if (!m_pVehicle)
    {
        return;
    }

    // we drive it: the engine is ours. Until the server makes us its syncer the old one's idle updates come on, the
    // engine they said off stopped the one our enter started - a car that wouldn't go until got in again
    if (!m_pVehicle->m_pDriver || m_pVehicle->m_pDriver != FindPlayerPed(0))
    {
        m_pVehicle->m_nVehicleFlags.bEngineOn = extras.engineOn;
    }
    m_pVehicle->m_nVehicleFlags.bLightsOn = extras.lightsOn;
    m_pVehicle->m_nVehicleFlags.bSirenOrAlarm = extras.siren;
    m_pVehicle->m_nOverrideLights = extras.overrideLights;
    // CVehicle::ProcessCarAlarm (0x6D21F0) counts it down to 0; kept going while the syncer's one does
    if (extras.alarm && (m_pVehicle->m_nAlarmState == 0 || m_pVehicle->m_nAlarmState == ALARM_STATE_ARMED))
    {
        m_pVehicle->m_nAlarmState = ALARM_DURATION;
    }
    else if (!extras.alarm && m_pVehicle->m_nAlarmState != ALARM_STATE_ARMED)
    {
        m_pVehicle->m_nAlarmState = 0;
    }
    if (m_pVehicle->m_nVehicleSubType == VEHICLE_HELI)
    {
        VehicleField<bool>(m_pVehicle, OFFSET_SEARCH_LIGHT) = extras.heliSearchLight;
    }
    if (m_pVehicle->m_nVehicleSubType == VEHICLE_PLANE)
    {
        VehicleField<uint8_t>(m_pVehicle, OFFSET_SMOKE_TRAIL) = extras.smokeTrail ? 1 : 0;
    }

    m_bHasTurret = extras.hasTurret;
    m_fTurretHorizontal = extras.turretHorizontal;
    m_fTurretVertical = extras.turretVertical;

    if (m_pVehicle->m_nVehicleType != VEHICLE_AUTOMOBILE)
    {
        return;
    }

    // from where the doors are drawn now to the syncer's over the time to the next update
    CAutomobile* pAutomobile = static_cast<CAutomobile*>(m_pVehicle);
    for (int i = 0; i < 6; i++)
    {
        m_afDoorStart[i] = pAutomobile->m_doors[i].GetAngleOpenRatio();
        m_afDoorTarget[i] = extras.doors[i] / 255.0f;
    }
    unsigned int nDuration = std::clamp(CTimer::m_snTimeInMilliseconds - m_nLastTargetTime, 50u, 400u);
    for (int i = 0; i < 6; i++)
    {
        m_anDoorStartTime[i] = CTimer::m_snTimeInMilliseconds;
        m_anDoorDuration[i] = nDuration;
    }
    m_bHasDoors = true;
}

void CNetworkVehicle::ProcessExtras()
{
    if (!m_pVehicle)
    {
        return;
    }

    if (m_bHasTurret)
    {
        VehicleField<float>(m_pVehicle, OFFSET_TURRET) = m_fTurretHorizontal;
        VehicleField<float>(m_pVehicle, OFFSET_TURRET + 4) = m_fTurretVertical;
    }

    if (!m_bHasDoors || m_pVehicle->m_nVehicleType != VEHICLE_AUTOMOBILE)
    {
        return;
    }

    CAutomobile* pAutomobile = static_cast<CAutomobile*>(m_pVehicle);
    uint8_t nFlags = m_pVehicle->m_nGettingInFlags | m_pVehicle->m_nGettingOutFlags;
    uint8_t nInUse = GetDoorsOfEnterTask(FindPlayerPed(0), m_pVehicle);
    for (auto pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        nInUse |= GetDoorsOfEnterTask(pNetworkPlayer->m_pPed, m_pVehicle);
    }
    // our copies of an npc's enter (PED_VEHICLE_ENTER): the syncer's ped is a ping ahead, its door would open and
    // shut under our copy's open door anim
    for (auto pNetworkPed : CNetworkPedManager::m_pPeds)
    {
        if (!pNetworkPed->m_bSyncing && pNetworkPed->m_enterVehicleId == m_vehicleId &&
            pNetworkPed->IsEnteringVehicle(m_pVehicle))
        {
            nInUse |= GetDoorsOfEnterTask(pNetworkPed->m_pPed, m_pVehicle);
        }
    }
    for (int i = 0; i < 6; i++)
    {
        if (nFlags & DOOR_ENTER_EXIT_FLAGS[i])
        {
            nInUse |= 1 << i;
        }
    }
    for (int i = 0; i < 6; i++)
    {
        uint8_t nStatus = pAutomobile->m_damageManager.m_anDoorsStatus[i];
        if (nStatus == 4 || !pAutomobile->m_aCarNodes[DOOR_NODES[i]])  // missing
        {
            continue;
        }

        if (nInUse & (1 << i))
        {
            m_anDoorFreeUntil[i] = CTimer::m_snTimeInMilliseconds + DOOR_FREE_AFTER_ENTER_EXIT;
        }
        if (CTimer::m_snTimeInMilliseconds < m_anDoorFreeUntil[i])
        {
            // afterwards from where the task left it to the syncer's
            m_afDoorStart[i] = pAutomobile->m_doors[i].GetAngleOpenRatio();
            m_anDoorStartTime[i] = CTimer::m_snTimeInMilliseconds;
            m_anDoorDuration[i] = DOOR_BLEND_BACK;
            continue;
        }

        float fStep = std::min(static_cast<float>(CTimer::m_snTimeInMilliseconds - m_anDoorStartTime[i]) /
                                   static_cast<float>(std::max(m_anDoorDuration[i], 1u)),
            1.0f);
        float fRatio = m_afDoorStart[i] + (m_afDoorTarget[i] - m_afDoorStart[i]) * fStep;
        if (std::fabs(pAutomobile->m_doors[i].GetAngleOpenRatio() - fRatio) > 0.001f)
        {
            pAutomobile->OpenDoor(nullptr, DOOR_NODES[i], static_cast<eDoors>(i), fRatio, false);
        }
    }
}
