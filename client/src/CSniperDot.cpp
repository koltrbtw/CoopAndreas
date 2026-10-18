#include "stdafx.h"
#include "CSniperDot.h"
#include <CCoronas.h>

static constexpr float DOT_RANGE = 250.0f;
// the corona's size in metres, by its distance from our camera: a fixed 0.12 m was a couple of faint pixels at 20 m,
// lost in daylight. About the same size on the screen at any distance
static constexpr float DOT_SIZE_PER_METRE = 0.015f;
static constexpr float MIN_DOT_SIZE = 0.1f;
static constexpr float MAX_DOT_SIZE = 3.0f;
static constexpr unsigned int CORONA_ID_BASE = 0xC00A0000;  // + the player's internal id, clear of the game's ids

void CSniperDot::Process()
{
    for (CNetworkPlayer* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        CPlayerPed* pPed = pNetworkPlayer->m_pPed;
        if (!pPed || pPed->m_nPedFlags.bInVehicle || pPed->GetWeapon().m_eWeaponType != WEAPON_SNIPERRIFLE)
        {
            continue;
        }

        const Packets::Players::PlayerCameraSync& camera = pNetworkPlayer->m_cameraSnapshot;
        if (camera.cameraMode != MODE_SNIPER)
        {
            continue;
        }

        CVector front = camera.front;
        front.Normalise();
        CVector start = camera.source;
        CVector end = start + front * DOT_RANGE;

        CColPoint colPoint{};
        CEntity* pHit = nullptr;
        CEntity* pIgnored = CWorld::pIgnoreEntity;
        CWorld::pIgnoreEntity = pPed;
        bool bHit = CWorld::ProcessLineOfSight(start, end, colPoint, pHit, true, true, true, true, false, false, false, true);
        CWorld::pIgnoreEntity = pIgnored;
        if (!bHit)
        {
            continue;
        }

        // a little off the surface, or the corona sinks into it
        CVector dot = colPoint.m_vecPoint - front * 0.05f;
        float size = std::clamp((dot - TheCamera.GetPosition()).Magnitude() * DOT_SIZE_PER_METRE, MIN_DOT_SIZE, MAX_DOT_SIZE);
        // a solid blob (the headlight one), the shiny star faded out into its thin rays
        CCoronas::RegisterCorona(CORONA_ID_BASE + pNetworkPlayer->GetInternalId(), nullptr, 255, 0, 0, 255, dot, size,
            DOT_RANGE, CORONATYPE_HEADLIGHT, FLARETYPE_NONE, false, false, 0, 0.0f, false, 0.1f, 0, 15.0f, false, false);
    }
}
