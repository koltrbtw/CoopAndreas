#include "stdafx.h"
#include "CNetworkVehicle.h"
#include "CTrainSync.h"

CNetworkVehicle::CNetworkVehicle(vehicleid_t vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2, unsigned char createdBy)
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
    m_nModelId = modelid;
    m_nCreatedBy = createdBy;

    if (!CreateVehicle(vehicleid, modelid, pos, rotation, color1, color2))
        return;

}

bool CNetworkVehicle::CreateVehicle(vehicleid_t vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2)
{
    unsigned char oldFlags = CStreaming::ms_aInfoForModel[modelid].m_nFlags;
    CStreaming::RequestModel(modelid, GAME_REQUIRED);
    CStreaming::LoadAllRequestedModels(false);

    if (!(oldFlags & GAME_REQUIRED))
    {
        CStreaming::SetModelIsDeletable(modelid);
        CStreaming::SetModelTxdIsDeletable(modelid);
    }

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

    case VEHICLE_BIKE:
        m_pVehicle = new CBike(modelid, MISSION_VEHICLE);
        ((CBike*)m_pVehicle)->m_nDamageFlags |= 0x10; break;

    case VEHICLE_BMX:
        m_pVehicle = new CBmx(modelid, MISSION_VEHICLE);
        ((CBmx*)m_pVehicle)->m_nDamageFlags |= 0x10; break;

    case VEHICLE_TRAILER:
        m_pVehicle = new CTrailer(modelid, MISSION_VEHICLE); break;

    case VEHICLE_BOAT:
        m_pVehicle = new CBoat(modelid, MISSION_VEHICLE); break;

    case VEHICLE_TRAIN:
        return false;

    default:
        m_pVehicle = new CAutomobile(modelid, MISSION_VEHICLE, true); break;
    }

    if (!m_pVehicle)
        return false;

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
            CWorld::Remove(m_pVehicle);
            //CWorld::RemoveReferencesToDeletedObject(m_pVehicle); // ?
            delete m_pVehicle;
        }
    }
    m_pVehicle = nullptr;
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
    GetPacketFactory().Send(vehicleSpawnPacket);

    return networkVehicle;
}

void CNetworkVehicle::BecomeSyncer(const Packets::Vehicles::VehicleHandoff* pHandoff)
{
    m_bSyncing = true;
    m_nOwnershipChangeTime = CTimer::m_snTimeInMilliseconds;

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
        m_pVehicle->m_matrix->pos = pHandoff->pos;
        m_pVehicle->m_matrix->right = pHandoff->roll;
        m_pVehicle->m_matrix->up = pHandoff->rot;
        m_pVehicle->m_vecMoveSpeed = pHandoff->velocity;
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

        // otherwise after the vehicle comes back to us it would follow a stale route
        m_pVehicle->m_autoPilot.m_nCarMission = MISSION_NONE;
        m_pVehicle->m_autoPilot.m_nTempAction = 0;
    }

    m_bSyncing = false;
    m_nOwnershipChangeTime = CTimer::m_snTimeInMilliseconds;

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
