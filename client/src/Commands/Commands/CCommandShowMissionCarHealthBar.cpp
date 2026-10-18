#include "stdafx.h"
#include "CCommandShowMissionCarHealthBar.h"
#include "CMissionCarHealthBar.h"
#include "network/packets/scripts.h"

static vehicleid_t GetVehicleId(int nHandle)
{
    CVehicle* pVehicle = CPools::GetVehicle(nHandle);
    CNetworkVehicle* pNetworkVehicle = pVehicle ? CNetworkVehicleManager::GetVehicle(pVehicle) : nullptr;
    return pNetworkVehicle ? pNetworkVehicle->m_vehicleId : vehicleid_t{};
}

// Coop.ShowMissionCarHealthBar(player_actor, $counter_var, 'GXT', car1, car2)
void CCommandShowMissionCarHealthBar::Process(CRunningScript* script)
{
    script->CollectParameters(1);
    CPed* pPed = CPools::GetPed(ScriptParams[0]);

    // the counter's variable as DISPLAY_ONSCREEN_COUNTER reads it: a global, at the same offset in every client's main.scm
    auto pVariable = reinterpret_cast<uint8_t*>(script->GetPointerToScriptVariable(0));
    ptrdiff_t nOffset = pVariable - reinterpret_cast<uint8_t*>(CTheScripts::ScriptSpace);

    Packets::Scripts::NetworkPlayerCarHealthBar packet{};
    script->ReadTextLabelFromScript(packet.gxt, 8);
    packet.gxt[7] = '\0';

    script->CollectParameters(2);
    packet.car1 = GetVehicleId(ScriptParams[0]);
    packet.car2 = GetVehicleId(ScriptParams[1]);

    if (!pPed || nOffset < 0 || nOffset > UINT16_MAX)
    {
        return;
    }
    packet.varOffset = static_cast<uint16_t>(nOffset);
    packet.show = true;

    if (pPed == FindPlayerPed(0))
    {
        CMissionCarHealthBar::Set(packet.varOffset, packet.gxt, packet.car1, packet.car2);
    }
    else if (auto pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPed))
    {
        packet.forWhoPlayerId = pNetworkPlayer->m_playerId;
        GetPacketFactory().Send(packet);
    }
}
