#include "stdafx.h"
#include "CVehicleBlowSync.h"

// CVehicle::BlowUpCar(CEntity* damager, bool bHideExplosionFx), its vtable slot
static constexpr unsigned int BLOW_UP_CAR_SLOT = 0xA4;

// every vehicle class with its own vtable (CAutomobile and its heirs share 0x6B3780, a monster truck, a quad, a
// trailer)
static constexpr uintptr_t VEHICLE_VTABLES[] = {
    0x871120,  // CAutomobile
    0x8721A0,  // CBoat
    0x871360,  // CBike
    0x871948,  // CPlane
    0x871680,  // CHeli
    0x871528,  // CBmx
    0x8717D8,  // CMonsterTruck
    0x871AE8,  // CQuadBike
    0x872370,  // CTrain
    0x871C28,  // CTrailer
};

struct BlowUpCarHook
{
    uintptr_t vtable;
    uintptr_t original;
};

static BlowUpCarHook s_aHooks[std::size(VEHICLE_VTABLES)];

static uintptr_t GetOriginal(CVehicle* pVehicle)
{
    uintptr_t vtable = *reinterpret_cast<uintptr_t*>(pVehicle);
    for (const BlowUpCarHook& hook : s_aHooks)
    {
        if (hook.vtable == vtable)
        {
            return hook.original;
        }
    }
    return 0;
}

static void CallOriginal(CVehicle* pVehicle, CEntity* pDamager, bool bHideExplosion)
{
    if (uintptr_t original = GetOriginal(pVehicle))
    {
        reinterpret_cast<void(__thiscall*)(CVehicle*, CEntity*, bool)>(original)(pVehicle, pDamager, bHideExplosion);
    }
}

static void __fastcall CVehicle__BlowUpCar_Hook(CVehicle* This, SKIP_EDX, CEntity* pDamager, bool bHideExplosion)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(This);
    if (!pNetworkVehicle || CVehicleBlowSync::IsFromNetwork())
    {
        CallOriginal(This, pDamager, bHideExplosion);
        return;
    }

    // a clone burns as its syncer's one does (the health comes with its updates), it blows up on the packet only
    if (!pNetworkVehicle->m_bSyncing)
    {
        return;
    }

    bool bWasWrecked = This->m_nStatus == STATUS_WRECKED;
    CallOriginal(This, pDamager, bHideExplosion);
    if (bWasWrecked || This->m_nStatus != STATUS_WRECKED)
    {
        return;
    }

    Packets::Vehicles::VehicleBlown packet{};
    packet.vehicleid = pNetworkVehicle->m_vehicleId;
    packet.hideExplosion = bHideExplosion;
    GetPacketFactory().Send(packet);
}

void CVehicleBlowSync::InjectHooks()
{
    for (size_t i = 0; i < std::size(VEHICLE_VTABLES); i++)
    {
        uintptr_t slot = VEHICLE_VTABLES[i] + BLOW_UP_CAR_SLOT;
        s_aHooks[i] = {VEHICLE_VTABLES[i], *reinterpret_cast<uintptr_t*>(slot)};
        patch::SetPointer(slot, CVehicle__BlowUpCar_Hook);
    }
}

void CVehicleBlowSync::HandlePacket(const Packets::Vehicles::VehicleBlown& packet)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(packet.vehicleid);
    if (!pNetworkVehicle || !pNetworkVehicle->m_pVehicle || pNetworkVehicle->m_bSyncing)
    {
        return;
    }

    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    if (pVehicle->m_nStatus == STATUS_WRECKED)
    {
        return;
    }

    ms_bFromNetwork = true;
    CallOriginal(pVehicle, nullptr, packet.hideExplosion);
    ms_bFromNetwork = false;
}

void CVehicleBlowSync::MakeWreck(CVehicle* pVehicle)
{
    if (pVehicle->m_nStatus == STATUS_WRECKED)
    {
        return;
    }

    ms_bFromNetwork = true;
    // the virtual BlowUpCarCutSceneNoExtras (CAutomobile's 0x6B3BB0): the car parts and the scorched look of
    // BlowUpCar; no cam shake, no flying parts, no explosion and fire (the third one skips them, 0x6B3DE5), no sound
    pVehicle->BlowUpCarCutSceneNoExtras(true, true, true, false);
    if (pVehicle->m_nStatus != STATUS_WRECKED)
    {
        // what BlowUpCarCutSceneNoExtras does to the entity: STATUS_WRECKED in CEntity::m_nStatus (byte 0x36,
        // or 0x28 at 0x6B3C25) and the scorched render flag (or [0x40], 0x20000000 at 0x6B3C2B)
        pVehicle->m_nStatus = STATUS_WRECKED;
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(pVehicle) + 0x40) |= 0x20000000;
        pVehicle->m_fHealth = 0.0f;
        pVehicle->m_nVehicleFlags.bEngineOn = false;
    }
    ms_bFromNetwork = false;
}
