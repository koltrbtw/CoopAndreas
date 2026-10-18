#include "stdafx.h"
#include "CPickupSync.h"
#include <CPickup.h>
#include <CPickups.h>

using namespace Packets::Pickups;

static constexpr int PICKUP_COUNT = 620;
static constexpr unsigned int REQUEST_INTERVAL = 250;  // ms, while we stand on it
static constexpr unsigned int GRANT_TIME = 1000;       // ms, renewed by the server on every request
static constexpr unsigned int PENDING_CHECK = 1000;    // ms

struct SPending
{
    PickupKey key;
    int32_t remainingMs;
    unsigned int receivedTime;
};

static PickupKey s_grantKey{};
static unsigned int s_grantUntil = 0;
static PickupKey s_requestKey{};
static unsigned int s_lastRequestTime = 0;
static std::vector<SPending> s_pending;  // states of the pickups our scripts haven't made yet
static unsigned int s_lastPendingCheck = 0;

// what a body, a jetpack or a gang war drops: one game makes it, the others a copy
static bool IsDropped(uint8_t type)
{
    return type == PICKUP_ONCE_TIMEOUT || type == PICKUP_ONCE_TIMEOUT_SLOW || type == PICKUP_MONEY ||
           type == PICKUP_ONCE_FOR_MISSION;
}

static bool IsSynced(const CPickup& pickup)
{
    return pickup.m_nPickupType == PICKUP_ON_STREET || pickup.m_nPickupType == PICKUP_ON_STREET_SLOW ||
           pickup.m_nPickupType == PICKUP_ONCE || pickup.m_nPickupType == PICKUP_MONEY_DOESNTDISAPPEAR ||
           IsDropped(pickup.m_nPickupType);
}

// the host's scripts made them (the missions run there): the others got copies, a copy taken goes into the host's
// collected array for its script's HAS_PICKUP_BEEN_COLLECTED
static std::vector<PickupKey> s_hostScriptKeys;

static bool IsHostScriptKey(const PickupKey& key)
{
    return std::find(s_hostScriptKeys.begin(), s_hostScriptKeys.end(), key) != s_hostScriptKeys.end();
}

static PickupKey GetKey(const CPickup& pickup)
{
    // CPickup::m_vecPos at +0x10: 3 x int16, 1/8 m
    const int16_t* pPos = reinterpret_cast<const int16_t*>(reinterpret_cast<const uint8_t*>(&pickup) + 0x10);
    PickupKey key{};
    key.model = pickup.m_nModelIndex;
    key.type = pickup.m_nPickupType;
    key.x = pPos[0];
    key.y = pPos[1];
    key.z = pPos[2];
    return key;
}

// the engine's own test is in CPickup::Update (on foot: |dz| < 2, 2D distance < sqrt(1.8); a bribe in a vehicle: 2 m)
static bool IsStandingOn(CPickup& pickup, CPlayerPed* pPlayerPed, CVehicle* pVehicle)
{
    CVector position = pVehicle ? pVehicle->GetPosition() : pPlayerPed->GetPosition();
    CVector delta = position - pickup.GetPosn();
    return std::fabs(delta.z) < 2.0f && delta.x * delta.x + delta.y * delta.y < (pVehicle ? 4.0f : 1.8f);
}

static constexpr int MODEL_OYSTER = 953;
static constexpr int MODEL_HORSESHOE = 954;

static void ApplyState(CPickup& pickup, int32_t remainingMs)
{
    PickupKey key = GetKey(pickup);
    if (IsHostScriptKey(key))
    {
        int index = static_cast<int>(&pickup - CPickups::aPickUps);
        CPickups::AddToCollectedPickupsArray((static_cast<uint16_t>(pickup.m_nReferenceIndex) << 16) | index);
        if (remainingMs == PickupCollected::FOREVER)
        {
            s_hostScriptKeys.erase(std::find(s_hostScriptKeys.begin(), s_hostScriptKeys.end(), key));
        }
    }

    if (remainingMs == PickupCollected::FOREVER)
    {
        // a collectible: the collector's game counted it (PickedUpHorseShoe, PickedUpOyster, PictureTaken), the money
        // came as shared money; the others count it too, for the "N of 50" and the 100% stats
        if (pickup.m_nModelIndex == MODEL_HORSESHOE)
        {
            CStats::IncrementStat(STAT_HORSESHOES_COLLECTED, 1.0f);
        }
        else if (pickup.m_nModelIndex == MODEL_OYSTER)
        {
            CStats::IncrementStat(STAT_OYSTERS_COLLECTED, 1.0f);
        }
        else if (pickup.m_nPickupType == PICKUP_SNAPSHOT)
        {
            CStats::IncrementStat(STAT_SNAPSHOTS_TAKEN, 1.0f);
        }
        pickup.Remove();
        return;
    }

    pickup.GetRidOfObjects();
    pickup.m_nFlags.bDisabled = true;
    pickup.m_nRegenerationTime = CTimer::m_snTimeInMilliseconds + static_cast<unsigned int>(remainingMs);
}

// all slots with this key (two of a kind at one spot share the state)
template <typename F>
static bool ForEachSlot(const PickupKey& key, F&& callback)
{
    bool bFound = false;
    for (int i = 0; i < PICKUP_COUNT; i++)
    {
        CPickup& pickup = CPickups::aPickUps[i];
        if (pickup.m_nPickupType != PICKUP_NONE && GetKey(pickup) == key)
        {
            callback(pickup);
            bFound = true;
        }
    }
    return bFound;
}

// CPickups::Update -> CPickup::Update for our player (0x45902E): a synced pickup is ours to take after the grant only
static bool __fastcall CPickup__Update_Hook(
    CPickup* This, SKIP_EDX, CPlayerPed* pPlayerPed, CVehicle* pVehicle, int playerId)
{
    // a disabled one only regenerates here
    if (!CNetwork::m_bAuthenticated || !IsSynced(*This) || This->m_nFlags.bDisabled || !pPlayerPed)
    {
        return This->Update(pPlayerPed, pVehicle, playerId);
    }

    PickupKey key = GetKey(*This);
    if (key == s_grantKey && CTimer::m_snTimeInMilliseconds < s_grantUntil)
    {
        bool bCollected = This->Update(pPlayerPed, pVehicle, playerId);
        if (bCollected)
        {
            // on street: CPickup::Update set the regeneration time; once: it removed the pickup
            PickupCollected packet{};
            packet.key = key;
            bool bRegenerates = key.type == PICKUP_ON_STREET || key.type == PICKUP_ON_STREET_SLOW;
            packet.regenerationMs =
                bRegenerates ? static_cast<int32_t>(This->m_nRegenerationTime - CTimer::m_snTimeInMilliseconds)
                             : PickupCollected::FOREVER;
            GetPacketFactory().Send(packet);
            s_grantUntil = 0;
            logger::info("pickup %d (type %d) collected, back in %d ms", key.model, key.type, packet.regenerationMs);
        }
        return bCollected;
    }

    if (IsStandingOn(*This, pPlayerPed, pVehicle) &&
        (!(key == s_requestKey) || CTimer::m_snTimeInMilliseconds - s_lastRequestTime >= REQUEST_INTERVAL))
    {
        s_requestKey = key;
        s_lastRequestTime = CTimer::m_snTimeInMilliseconds;
        PickupCollectRequest packet{};
        packet.key = key;
        GetPacketFactory().Send(packet);
    }
    return false;
}

// CWeapon::TakePhotograph -> CPickups::PictureTaken (0x73C243): the snapshot in the picture is removed there and then,
// no CPickup::Update, so it goes to the others after it (the server takes it as it is: two photos of it within a
// ping both count)
static void __cdecl CPickups__PictureTaken_Hook()
{
    std::vector<std::pair<int, PickupKey>> snapshots;
    for (int i = 0; i < PICKUP_COUNT; i++)
    {
        if (CPickups::aPickUps[i].m_nPickupType == PICKUP_SNAPSHOT)
        {
            snapshots.emplace_back(i, GetKey(CPickups::aPickUps[i]));
        }
    }

    CPickups::PictureTaken();

    for (const auto& [index, key] : snapshots)
    {
        if (CNetwork::m_bAuthenticated && CPickups::aPickUps[index].m_nPickupType != PICKUP_SNAPSHOT)
        {
            PickupCollected packet{};
            packet.key = key;
            packet.regenerationMs = PickupCollected::FOREVER;
            GetPacketFactory().Send(packet);
            logger::info("snapshot at %d %d %d taken", key.x, key.y, key.z);
        }
    }
}

// a pickup our game dropped (or our script made, we are the host) goes to the others
static void SendSpawn(int handle, bool bByHostScript = false)
{
    int index = handle & 0xFFFF;  // tPickupReference: (refIndex << 16) | index
    if (!CNetwork::m_bAuthenticated || handle == -1 || index >= PICKUP_COUNT)
    {
        return;
    }

    CPickup& pickup = CPickups::aPickUps[index];
    if (!IsSynced(pickup) || (!bByHostScript && !IsDropped(pickup.m_nPickupType)))
    {
        return;
    }
    if (bByHostScript)
    {
        s_hostScriptKeys.push_back(GetKey(pickup));
    }

    PickupSpawn packet{};
    packet.key = GetKey(pickup);
    packet.ammo = pickup.m_nAmmo;
    packet.isEmpty = pickup.m_nFlags.bEmpty;
    bool bTimeout = pickup.m_nPickupType == PICKUP_ONCE_TIMEOUT || pickup.m_nPickupType == PICKUP_ONCE_TIMEOUT_SLOW ||
                    pickup.m_nPickupType == PICKUP_MONEY;
    packet.remainingMs = bTimeout ? static_cast<int32_t>(pickup.m_nRegenerationTime - CTimer::m_snTimeInMilliseconds)
                                  : PickupSpawn::NO_TIMEOUT;
    GetPacketFactory().Send(packet);
}

// CPickups::GenerateNewOne from CreateSomeMoney (0x458A58), DropJetPack (0x67B6DE), the gang war waves (0x445098,
// 0x445AFD)
static int __cdecl CPickups__GenerateNewOne_Hook(CVector coors, unsigned int modelId, unsigned char pickupType,
    unsigned int ammo, unsigned int moneyPerDay, bool isEmpty, char* message)
{
    int handle = CPickups::GenerateNewOne(coors, modelId, pickupType, ammo, moneyPerDay, isEmpty, message);
    SendSpawn(handle);
    return handle;
}

// CPickups::GenerateNewOne_WeaponType from CPed::CreateDeadPedWeaponPickups (0x4592F7)
static int __cdecl CPickups__GenerateNewOne_WeaponType_Hook(
    CVector coors, eWeaponType weaponType, unsigned char pickupType, unsigned int ammo, bool isEmpty, char* message)
{
    int handle = CPickups::GenerateNewOne_WeaponType(coors, weaponType, pickupType, ammo, isEmpty, message);
    SendSpawn(handle);
    return handle;
}

// CPickups::GenerateNewOne from the pickup opcodes (CREATE_PICKUP, CREATE_PICKUP_WITH_AMMO, CREATE_MONEY_PICKUP...;
// not 0x48B243, the asset revenue one): the host's missions make them
static int __cdecl CPickups__GenerateNewOne_Script_Hook(CVector coors, unsigned int modelId, unsigned char pickupType,
    unsigned int ammo, unsigned int moneyPerDay, bool isEmpty, char* message)
{
    int handle = CPickups::GenerateNewOne(coors, modelId, pickupType, ammo, moneyPerDay, isEmpty, message);
    if (CLocalPlayer::m_bIsHost)
    {
        SendSpawn(handle, true);
    }
    return handle;
}

// CPickups::RemovePickUp from REMOVE_PICKUP (0x47E6BB): the host's script took its pickup away, the copies go too
static void __cdecl CPickups__RemovePickUp_Hook(int handle)
{
    int index = CPickups::GetActualPickupIndex(handle);
    PickupKey key{};
    bool bHostScript = index >= 0 && index < PICKUP_COUNT && IsHostScriptKey(key = GetKey(CPickups::aPickUps[index]));

    CPickups::RemovePickUp(handle);

    if (bHostScript && CNetwork::m_bAuthenticated)
    {
        s_hostScriptKeys.erase(std::find(s_hostScriptKeys.begin(), s_hostScriptKeys.end(), key));
        PickupCollected packet{};
        packet.key = key;
        packet.regenerationMs = PickupCollected::FOREVER;
        GetPacketFactory().Send(packet);
    }
}

void CPickupSync::InjectHooks()
{
    for (uintptr_t callSite : {0x477983u, 0x47E636u, 0x480658u, 0x481744u, 0x48CEB9u, 0x156DF52u})
    {
        patch::RedirectCall(callSite, CPickups__GenerateNewOne_Script_Hook);
    }
    patch::RedirectCall(0x47E6BB, CPickups__RemovePickUp_Hook);

    patch::RedirectCall(0x45902E, CPickup__Update_Hook);

    for (uintptr_t callSite : {0x458A58u, 0x67B6DEu, 0x445098u, 0x445AFDu})
    {
        patch::RedirectCall(callSite, CPickups__GenerateNewOne_Hook);
    }
    patch::RedirectCall(0x4592F7, CPickups__GenerateNewOne_WeaponType_Hook);
    patch::RedirectCall(0x73C243, CPickups__PictureTaken_Hook);
}

void CPickupSync::HandleSpawn(const PickupSpawn& packet)
{
    // a copy already (a late join list after a reconnect)
    if (ForEachSlot(packet.key, [](CPickup&) {}))
    {
        return;
    }

    CStreaming::RequestModel(packet.key.model, 0);
    CStreaming::LoadAllRequestedModels(false);

    CVector position(packet.key.x / 8.0f, packet.key.y / 8.0f, packet.key.z / 8.0f);
    int handle = CPickups::GenerateNewOne(
        position, packet.key.model, packet.key.type, packet.ammo, 0, packet.isEmpty, nullptr);
    int index = handle & 0xFFFF;
    if (handle == -1 || index >= PICKUP_COUNT)
    {
        return;
    }

    // its own timeout from now (GenerateNewOne set the full one)
    if (packet.remainingMs != PickupSpawn::NO_TIMEOUT)
    {
        CPickups::aPickUps[index].m_nRegenerationTime =
            CTimer::m_snTimeInMilliseconds + static_cast<unsigned int>(packet.remainingMs);
    }
}

void CPickupSync::HandleGrant(const PickupGrant& packet)
{
    s_grantKey = packet.key;
    s_grantUntil = CTimer::m_snTimeInMilliseconds + GRANT_TIME;
}

void CPickupSync::HandleState(const PickupState& packet)
{
    if (packet.key == s_grantKey)
    {
        s_grantUntil = 0;
    }

    bool bFound = ForEachSlot(packet.key, [&](CPickup& pickup) { ApplyState(pickup, packet.remainingMs); });
    logger::info("pickup %d (type %d) taken by someone, back in %d ms%s", packet.key.model, packet.key.type,
        packet.remainingMs, bFound ? "" : ", not made here yet");
    if (!bFound)
    {
        s_pending.push_back({packet.key, packet.remainingMs, CTimer::m_snTimeInMilliseconds});
    }
}

void CPickupSync::Process()
{
    if (s_pending.empty() || CTimer::m_snTimeInMilliseconds - s_lastPendingCheck < PENDING_CHECK)
    {
        return;
    }
    s_lastPendingCheck = CTimer::m_snTimeInMilliseconds;

    for (auto it = s_pending.begin(); it != s_pending.end();)
    {
        int32_t remaining = it->remainingMs;
        if (remaining != PickupCollected::FOREVER)
        {
            remaining -= static_cast<int32_t>(CTimer::m_snTimeInMilliseconds - it->receivedTime);
            if (remaining <= 0)
            {
                it = s_pending.erase(it);
                continue;
            }
        }

        if (ForEachSlot(it->key, [&](CPickup& pickup) { ApplyState(pickup, remaining); }))
        {
            it = s_pending.erase(it);
            continue;
        }
        ++it;
    }
}

void CPickupSync::Reset()
{
    s_pending.clear();
    s_hostScriptKeys.clear();
    s_grantUntil = 0;
}
