#include "stdafx.h"
#include "DamageHooks.h"
#include <eEventType.h>

// a clone: a remote player's ped or a ped someone else syncs
static bool IsClone(CPed* pPed)
{
    if (CNetworkPlayerManager::GetPlayer(pPed))
    {
        return true;
    }
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPed);
    return pNetworkPed && !pNetworkPed->m_bSyncing;
}

// the damage a clone takes, its fall, its bike knock off, its death come from its owner (health and anims in the
// updates, PLAYER_FALL, VEHICLE_EXIT, PED_DIED): a local hit would count the damage twice and make the clone fall
// or die on its own, while the owner's ped stands. Without AffectsPed there is no damage response, no event, no
// reaction task (as MTA)
static bool __fastcall CEventDamage__AffectsPed_Hook(CEventDamage* This, SKIP_EDX, CPed* pPed)
{
    if (CNetwork::m_bAuthenticated && pPed && IsClone(pPed))
    {
        return false;
    }
    return plugin::CallMethodAndReturn<bool, 0x4B35A0, CEventDamage*, CPed*>(This, pPed);
}

// a vehicle hitting a clone: the impulse, the damage and the knock down are the owner's to decide (CPhysical::
// ProcessCollisionSectorList, CCollisionEventScanner); our clone of the vehicle is a moment off anyway
static void __fastcall CPed__KillPedWithCar_Hook(CPed* This, SKIP_EDX, CVehicle* pVehicle, float fDamageIntensity,
    bool bPlayDeadAnimation)
{
    if (CNetwork::m_bAuthenticated && IsClone(This))
    {
        return;
    }
    plugin::CallMethod<0x5F0360, CPed*, CVehicle*, float, bool>(This, pVehicle, fDamageIntensity, bPlayDeadAnimation);
}

// a ped clone runs no ai of its own: collisions, gun shots, threats, a car on fire... would give it response tasks
// (dodge, flee, shake a fist, leave the car) its owner's ped doesn't play, the owner's anims and moves come in the
// updates, its death in PED_DIED
static bool __stdcall ShouldDropEvent(CPed* pPed, void* pEvent)
{
    // a hit and run (CEventVehicleHitAndRun, the vehicle at +0x10) of a vehicle deleted since: a clone gone or handed
    // over while the event waited in the global group. Its copy for every ped near registered a reference to nothing
    if (pEvent && plugin::CallVirtualMethodAndReturn<int, 1>(pEvent) == EVENT_VEHICLE_HIT_AND_RUN &&
        !*reinterpret_cast<CVehicle**>(static_cast<uint8_t*>(pEvent) + 0x10))
    {
        return true;
    }

    if (!pPed || !CNetwork::m_bAuthenticated)
    {
        return false;
    }

    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPed);
    return pNetworkPed && !pNetworkPed->m_bSyncing;
}

// CEventGroup::Add(CEvent*, bool), the entry: its 217 callers. The first 7 bytes (push ecx; push ebx; push esi;
// mov esi, [esp+10h]) are played here, then back to 0x4AB427; a dropped event returns nullptr as a ped it doesn't
// affect
static void __declspec(naked) CEventGroup__Add_Hook()
{
    __asm
    {
        pushad
        mov eax, [esp + 32 + 4]  // the event
        push eax
        push dword ptr [ecx]     // CEventGroup::m_pPed
        call ShouldDropEvent
        mov [esp + 28], eax      // eax of the pushad frame
        popad
        test al, al
        jnz drop

        push ecx
        push ebx
        push esi
        mov esi, [esp + 0x10]
        push 0x4AB427
        ret

    drop:
        xor eax, eax
        ret 8
    }
}

// the CEventVehicleHitAndRun ctor (0x4AE990), from its Clone (0x4B7100): the global event group hands its events to
// every ped by clones (0x4AB9C0), the vehicle of a hit and run may be deleted by then (its reference cleared) - a crash
// in CEntity::RegisterReference (0x571B73) on a null entity. The clone is dropped then (ShouldDropEvent)
static void __fastcall CEntity__RegisterReference_IfAny_Hook(CEntity* This, SKIP_EDX, CEntity** ppEntity)
{
    if (This)
    {
        This->RegisterReference(ppEntity);
    }
}

// CTaskSimpleDead::ProcessPed (0x630755, 0x63075C): the weapons and the money a body leaves are made by the game of
// its owner; every clone made its own, with its own random amount
static void __fastcall CPed__CreateDeadPedWeaponPickups_Hook(CPed* This)
{
    if (!CNetwork::m_bAuthenticated || !IsClone(This))
    {
        plugin::CallMethod<0x4591D0, CPed*>(This);
    }
}

static void __fastcall CPed__CreateDeadPedMoney_Hook(CPed* This)
{
    if (!CNetwork::m_bAuthenticated || !IsClone(This))
    {
        plugin::CallMethod<0x4590F0, CPed*>(This);
    }
}

void DamageHooks::InjectHooks()
{
    patch::RedirectCall(0x630755, CPed__CreateDeadPedWeaponPickups_Hook);
    patch::RedirectCall(0x63075C, CPed__CreateDeadPedMoney_Hook);

    patch::RedirectJump(0x4AB420, CEventGroup__Add_Hook);
    patch::RedirectCall(0x4AE9D3, CEntity__RegisterReference_IfAny_Hook);

    // every call of CEventDamage::AffectsPed (sa-disasm xref 0x4B35A0) and its vtable slot
    for (uintptr_t address : {0x439055, 0x4813CD, 0x4BA06F, 0x5680B8, 0x568271, 0x5E2F57, 0x5E3020, 0x5F0CBB, 0x5F0F25,
             0x6049B1, 0x604AA0, 0x6297FB, 0x65401F, 0x656974, 0x67C8FC, 0x686033, 0x68B44E, 0x6A929D, 0x6A94DF,
             0x6B6378, 0x6B6566, 0x6D83F5, 0x6DB30B, 0x6DBE7E, 0x73A687})
    {
        patch::RedirectCall(address, CEventDamage__AffectsPed_Hook);
    }
    patch::SetPointer(0x85AD8C, CEventDamage__AffectsPed_Hook);  // CEventDamage vtable (0x85AD78) + 5 * 4

    // every call of CPed::KillPedWithCar (sa-disasm xref 0x5F0360)
    for (uintptr_t address : {0x54C5CC, 0x54C642, 0x60461F, 0x60491B, 0x604A0D})
    {
        patch::RedirectCall(address, CPed__KillPedWithCar_Hook);
    }
}
