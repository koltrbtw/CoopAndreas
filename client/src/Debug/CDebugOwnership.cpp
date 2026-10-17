#include "stdafx.h"
#include "CDebugOwnership.h"
#include <map>

static constexpr float MAX_REQUEST_DISTANCE = 50.0f;
static constexpr float MAX_TRAFFIC_REQUEST_DISTANCE = 150.0f;

static bool IsKeyJustPressed(int nVirtKey, bool& bWasDown)
{
    bool bDown = (GetAsyncKeyState(nVirtKey) & 0x8000) != 0;
    bool bJustPressed = bDown && !bWasDown;
    bWasDown = bDown;
    return bJustPressed;
}

static CNetworkVehicle* FindNearestVehicle(const CVector& position, float fMaxDistance, bool bNpcDriverOnly)
{
    CNetworkVehicle* pNearest = nullptr;
    float fNearestDistance = fMaxDistance;

    for (auto pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (pNetworkVehicle->m_bSyncing || !pNetworkVehicle->m_pVehicle)
        {
            continue;
        }

        CPed* pDriver = pNetworkVehicle->m_pVehicle->m_pDriver;
        if (bNpcDriverOnly && (!pDriver || !CNetworkPedManager::GetPed(pDriver)))
        {
            continue;
        }

        float fDistance = (pNetworkVehicle->m_pVehicle->GetPosition() - position).Magnitude();
        if (fDistance < fNearestDistance)
        {
            pNearest = pNetworkVehicle;
            fNearestDistance = fDistance;
        }
    }

    return pNearest;
}

static void RequestNearestVehicle(const CVector& position)
{
    // driven traffic is the interesting case, it has to keep driving after the handoff
    CNetworkVehicle* pNearest = FindNearestVehicle(position, MAX_TRAFFIC_REQUEST_DISTANCE, true);
    if (!pNearest)
    {
        pNearest = FindNearestVehicle(position, MAX_REQUEST_DISTANCE, false);
    }

    if (!pNearest)
    {
        logger::info("ownership debug: no vehicle to request");
        return;
    }

    Packets::Vehicles::VehicleRequestOwnership packet{};
    packet.vehicleid = pNearest->m_vehicleId;
    GetPacketFactory().Send(packet);
    logger::info("ownership debug: requested vehicle %s", to_string(packet.vehicleid).c_str());
}

static void RequestNearestPed(const CVector& position)
{
    CNetworkPed* pNearest = nullptr;
    float fNearestDistance = MAX_REQUEST_DISTANCE;

    for (auto pNetworkPed : CNetworkPedManager::m_pPeds)
    {
        if (pNetworkPed->m_bSyncing || !pNetworkPed->m_pPed || pNetworkPed->m_pPed->m_nPedFlags.bInVehicle)
        {
            continue;
        }

        float fDistance = (pNetworkPed->m_pPed->GetPosition() - position).Magnitude();
        if (fDistance < fNearestDistance)
        {
            pNearest = pNetworkPed;
            fNearestDistance = fDistance;
        }
    }

    if (!pNearest)
    {
        logger::info("ownership debug: no ped to request");
        return;
    }

    Packets::Peds::PedRequestOwnership packet{};
    packet.pedid = pNearest->m_pedId;
    GetPacketFactory().Send(packet);
    logger::info("ownership debug: requested ped %s", to_string(packet.pedid).c_str());
}

static vehicleid_t ms_watchedVehicleId{};
static unsigned int ms_nWatchUntil = 0;

void CDebugOwnership::WatchVehicle(vehicleid_t vehicleid)
{
    ms_watchedVehicleId = vehicleid;
    ms_nWatchUntil = CTimer::m_snTimeInMilliseconds + 3000;
}

// a few seconds after a handoff: does the vehicle keep driving on its own?
static void ProcessWatchedVehicle()
{
    if (!ms_watchedVehicleId.valid() || CTimer::m_snTimeInMilliseconds < ms_nWatchUntil)
    {
        return;
    }

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(ms_watchedVehicleId);
    if (pNetworkVehicle && pNetworkVehicle->m_pVehicle)
    {
        CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
        CNetworkPed* pNetworkDriver = pVehicle->m_pDriver ? CNetworkPedManager::GetPed(pVehicle->m_pDriver) : nullptr;
        logger::info("ownership debug: vehicle %s 3 s after the handoff: syncing %d, speed %.1f, mission %d, status %d, "
                     "npc driver %d (ours %d)",
            to_string(ms_watchedVehicleId).c_str(), pNetworkVehicle->m_bSyncing, pVehicle->m_vecMoveSpeed.Magnitude() * 50.0f,
            pVehicle->m_autoPilot.m_nCarMission, pVehicle->m_nStatus, pNetworkDriver != nullptr,
            pNetworkDriver && pNetworkDriver->m_bSyncing);
    }
    else
    {
        logger::info("ownership debug: vehicle %s is gone 3 s after the handoff", to_string(ms_watchedVehicleId).c_str());
    }

    ms_watchedVehicleId = vehicleid_t();
}

void CDebugOwnership::Process()
{
    static bool bF5WasDown = false;
    static bool bF6WasDown = false;

    if (!CCore::ms_bDebug)
    {
        return;
    }

    ProcessWatchedVehicle();

    // how many network entities we have, ours and clones: the relevance and the handoffs at a glance
    static unsigned int nLastCountLog = 0;
    if (CTimer::m_snTimeInMilliseconds - nLastCountLog > 10000)
    {
        nLastCountLog = CTimer::m_snTimeInMilliseconds;
        auto CountOurs = [](const auto& entities)
        { return std::count_if(entities.begin(), entities.end(), [](auto e) { return e->m_bSyncing; }); };
        logger::info("ownership debug: network peds %d (ours %d), vehicles %d (ours %d)",
            (int)CNetworkPedManager::m_pPeds.size(), (int)CountOurs(CNetworkPedManager::m_pPeds),
            (int)CNetworkVehicleManager::m_pVehicles.size(), (int)CountOurs(CNetworkVehicleManager::m_pVehicles));
    }

    // a nearly full vehicle pool: what fills it and whose it is (a flood of clones of one model at one spot)
    static unsigned int nLastPoolLog = 0;
    if (CTimer::m_snTimeInMilliseconds - nLastPoolLog > 5000 && CPools::ms_pVehiclePool->GetNoOfUsedSpaces() > 200)
    {
        nLastPoolLog = CTimer::m_snTimeInMilliseconds;
        std::map<int, int> models;
        for (auto pVehicle : CPools::ms_pVehiclePool)
        {
            models[pVehicle->m_nModelIndex]++;
        }
        auto top = std::max_element(models.begin(), models.end(), [](auto& a, auto& b) { return a.second < b.second; });

        int ours = 0, clones = 0;
        CVector position{};
        for (auto pVehicle : CPools::ms_pVehiclePool)
        {
            if (pVehicle->m_nModelIndex == top->first)
            {
                position = pVehicle->GetPosition();
                if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle))
                {
                    (pNetworkVehicle->m_bSyncing ? ours : clones)++;
                }
            }
        }
        logger::warn("pool debug: %d vehicles, model %d x%d (ours %d, clones %d), one at %.1f %.1f %.1f",
            CPools::ms_pVehiclePool->GetNoOfUsedSpaces(), top->first, top->second, ours, clones, position.x, position.y,
            position.z);
    }

    if (GetForegroundWindow() != RsGlobal.ps->window)
    {
        return;
    }

    CPlayerPed* pLocalPlayer = FindPlayerPed(0);
    if (!pLocalPlayer)
    {
        return;
    }

    if (IsKeyJustPressed(VK_F5, bF5WasDown))
    {
        RequestNearestVehicle(pLocalPlayer->GetPosition());
    }

    if (IsKeyJustPressed(VK_F6, bF6WasDown))
    {
        RequestNearestPed(pLocalPlayer->GetPosition());
    }
}
