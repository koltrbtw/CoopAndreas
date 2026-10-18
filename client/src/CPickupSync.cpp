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

static bool IsSynced(const CPickup& pickup)
{
    return pickup.m_nPickupType == PICKUP_ON_STREET || pickup.m_nPickupType == PICKUP_ON_STREET_SLOW ||
           pickup.m_nPickupType == PICKUP_ONCE;
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

static void ApplyState(CPickup& pickup, int32_t remainingMs)
{
    if (remainingMs == PickupCollected::FOREVER)
    {
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
            packet.regenerationMs =
                key.type == PICKUP_ONCE
                    ? PickupCollected::FOREVER
                    : static_cast<int32_t>(This->m_nRegenerationTime - CTimer::m_snTimeInMilliseconds);
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

void CPickupSync::InjectHooks()
{
    patch::RedirectCall(0x45902E, CPickup__Update_Hook);
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
    s_grantUntil = 0;
}
