#include "stdafx.h"
#include "CMissionCarHealthBar.h"
#include <CUserDisplay.h>

void CMissionCarHealthBar::Set(uint16_t nVarOffset, const char* szGxt, vehicleid_t car1, vehicleid_t car2)
{
    Hide();
    ms_nVarOffset = nVarOffset;
    strncpy_s(ms_szGxt, szGxt, 7);
    ms_cars[0] = car1;
    ms_cars[1] = car2;
    ms_fFullHealth[0] = ms_fFullHealth[1] = 1000.0f;
    ms_bActive = true;
    logger::info("mission car health bar on: %s, %s", to_string(car1).c_str(), to_string(car2).c_str());
}

void CMissionCarHealthBar::Clear()
{
    Hide();
    ms_bActive = false;
}

void CMissionCarHealthBar::Hide()
{
    if (ms_bShown)
    {
        CUserDisplay::OnscnTimer.ClearCounter(ms_nVarOffset);
        ms_bShown = false;
    }
}

void CMissionCarHealthBar::Process()
{
    if (!ms_bActive)
    {
        return;
    }

    // the mission is over (passed, failed): the bar goes with it
    if (!CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag])
    {
        Clear();
        return;
    }

    CPlayerPed* pPlayer = FindPlayerPed(0);
    CVehicle* pVehicle = pPlayer && pPlayer->m_nPedFlags.bInVehicle ? pPlayer->m_pVehicle : nullptr;
    CNetworkVehicle* pNetworkVehicle = pVehicle ? CNetworkVehicleManager::GetVehicle(pVehicle) : nullptr;
    if (!pNetworkVehicle ||
        (pNetworkVehicle->m_vehicleId != ms_cars[0] && pNetworkVehicle->m_vehicleId != ms_cars[1]))
    {
        Hide();
        return;
    }

    // as the mission counts it: a car burns below 250. Full is the most it had: a mission gives its car more than
    // 1000 (SWEET4's Greenwood 1450), the bar stood at the top for the first hits then
    constexpr float BURNING_HEALTH = 250.0f;
    int nCar = pNetworkVehicle->m_vehicleId == ms_cars[0] ? 0 : 1;
    ms_fFullHealth[nCar] = std::max(ms_fFullHealth[nCar], pVehicle->m_fHealth);
    int nValue = static_cast<int>((pVehicle->m_fHealth - BURNING_HEALTH) * 100.0f / (ms_fFullHealth[nCar] - BURNING_HEALTH));
    *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[ms_nVarOffset]) = std::clamp(nValue, 0, 100);
    if (!ms_bShown)
    {
        CUserDisplay::OnscnTimer.AddCounter(ms_nVarOffset, 1, ms_szGxt, 0);  // 1: a bar
        ms_bShown = true;
    }
}
