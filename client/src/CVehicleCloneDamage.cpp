#include "stdafx.h"
#include "CVehicleCloneDamage.h"
#include "CVehicleVTables.h"

// CVehicle::BurstTyre(uint8 tyreComponentId, bool bPhysicalEffect), vtable slot 44
static constexpr unsigned int BURST_TYRE_SLOT = 0xB0;
// CVehicle::VehicleDamage(float intensity, uint16 component, CEntity* damager, CVector* coors, CVector* direction,
// eWeaponType), vtable slot 56
static constexpr unsigned int VEHICLE_DAMAGE_SLOT = 0xE0;

using BurstTyre = bool(__thiscall*)(CVehicle*, uint8_t, bool);
using VehicleDamage = void(__thiscall*)(CVehicle*, float, uint16_t, CEntity*, CVector*, CVector*, int);

static bool __fastcall CVehicle__BurstTyre_Hook(CVehicle* This, SKIP_EDX, uint8_t tyreComponentId, bool bPhysicalEffect);
static void __fastcall CVehicle__VehicleDamage_Hook(CVehicle* This, SKIP_EDX, float intensity, uint16_t component,
    CEntity* pDamager, CVector* pCoors, CVector* pDirection, int weapon);

static CVehicleVTables::Hook<decltype(&CVehicle__BurstTyre_Hook)> s_burstTyre;
static CVehicleVTables::Hook<decltype(&CVehicle__VehicleDamage_Hook)> s_vehicleDamage;

static bool IsClone(CVehicle* pVehicle)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle);
    return pNetworkVehicle && !pNetworkVehicle->m_bSyncing;
}

static bool __fastcall CVehicle__BurstTyre_Hook(CVehicle* This, SKIP_EDX, uint8_t tyreComponentId, bool bPhysicalEffect)
{
    if (IsClone(This))
    {
        return false;
    }
    BurstTyre original = s_burstTyre.Original<BurstTyre>(This);
    return original ? original(This, tyreComponentId, bPhysicalEffect) : false;
}

static void __fastcall CVehicle__VehicleDamage_Hook(CVehicle* This, SKIP_EDX, float intensity, uint16_t component,
    CEntity* pDamager, CVector* pCoors, CVector* pDirection, int weapon)
{
    if (IsClone(This))
    {
        return;
    }
    if (VehicleDamage original = s_vehicleDamage.Original<VehicleDamage>(This))
    {
        original(This, intensity, component, pDamager, pCoors, pDirection, weapon);
    }
}

// CVehicle::InflictDamage (0x6D7C90): fire (0x53A6F7), explosions (0x567BD9, 0x73647F), weapons (0x73B0BF, 0x73BEDD)
static void __fastcall CVehicle__InflictDamage_Hook(
    CVehicle* This, SKIP_EDX, CEntity* pDamager, int weapon, float intensity, CVector coords)
{
    if (IsClone(This))
    {
        return;
    }
    plugin::CallMethod<0x6D7C90, CVehicle*, CEntity*, int, float, CVector>(This, pDamager, weapon, intensity, coords);
}

void CVehicleCloneDamage::InjectHooks()
{
    s_burstTyre.Install(BURST_TYRE_SLOT, CVehicle__BurstTyre_Hook);
    s_vehicleDamage.Install(VEHICLE_DAMAGE_SLOT, CVehicle__VehicleDamage_Hook);

    for (uintptr_t callSite : {0x53A6F7u, 0x567BD9u, 0x73647Fu, 0x73B0BFu, 0x73BEDDu})
    {
        patch::RedirectCall(callSite, CVehicle__InflictDamage_Hook);
    }
}
