#include "stdafx.h"
#include "CWorldGridSync.h"
#include <CCarGenerator.h>
#include <CPathFind.h>

void CWorldGridSync::HandlePacket(Packets::World::WorldGridSectors* pPacket)
{
    ms_ownedSectors.reset();
    for (int i = 0; i < pPacket->count; i++)
    {
        ms_ownedSectors.set(pPacket->sectors[i]);
    }

    logger::info("world grid: we spawn the population in %d sectors", pPacket->count);
}

void CWorldGridSync::Reset()
{
    ms_ownedSectors.reset();
}

bool CWorldGridSync::CanSpawnAt(const CVector& position)
{
    // offline, alone, or inside an interior (no sectors there): the vanilla rules
    if (!CNetwork::m_bAuthenticated || CNetworkPlayerManager::m_pPlayers.empty() || CGame::currArea != 0)
    {
        return true;
    }

    return ms_ownedSectors.test(WorldGrid::GetSector(position.x, position.y));
}

// CCarCtrl::GenerateCarCreationCoors2 (0x424210): the spawn point of traffic, boats, police and emergency vehicles
static bool __cdecl CCarCtrl__GenerateCarCreationCoors2_Hook(CVector posn, float dirX, float dirY, float minDot,
    bool bInFront, float rVisible, float rHidden, CVector* pOutPos, CNodeAddress* pOutNode1, CNodeAddress* pOutNode2,
    float* pOutFrac, bool bUseFlag1NodeCache, bool bNoNthNodeStart)
{
    // radius instead of the camera: any direction (minDot -1), and an off screen point on the same ring as a visible
    // one instead of 38 units behind the player (CRadialPopulation keeps such a car alive at 0x43199F)
    dirX = 1.0f;
    dirY = 0.0f;
    minDot = -1.0f;
    bInFront = true;
    rHidden = rVisible;

    bool bFound = plugin::CallAndReturn<bool, 0x424210, CVector, float, float, float, bool, float, float, CVector*,
        CNodeAddress*, CNodeAddress*, float*, bool, bool>(posn, dirX, dirY, minDot, bInFront, rVisible, rHidden, pOutPos,
        pOutNode1, pOutNode2, pOutFrac, bUseFlag1NodeCache, bNoNthNodeStart);

    return bFound && CWorldGridSync::CanSpawnAt(*pOutPos);
}

// CPathFind::GeneratePedCreationCoors (0x44E790): the spawn point of peds on foot (single, couples, gangs, cops)
static bool __fastcall CPathFind__GeneratePedCreationCoors_Hook(CPathFind* This, int, float x, float y, float minVis,
    float maxVis, float minHid, float maxHid, CVector* pOutPos, CNodeAddress* pOutNode1, CNodeAddress* pOutNode2,
    float* pOutFrac, bool bAllowFlag20Nodes, RwMatrix* pCamMatrix)
{
    // radius instead of the camera: the off screen ring (15..25 units, so peds popped up right behind the player)
    // is the visible one
    minHid = minVis;
    maxHid = maxVis;

    bool bFound = plugin::CallMethodAndReturn<bool, 0x44E790, CPathFind*, float, float, float, float, float, float,
        CVector*, CNodeAddress*, CNodeAddress*, float*, bool, RwMatrix*>(This, x, y, minVis, maxVis, minHid, maxHid,
        pOutPos, pOutNode1, pOutNode2, pOutFrac, bAllowFlag20Nodes, pCamMatrix);

    return bFound && CWorldGridSync::CanSpawnAt(*pOutPos);
}

// CPopulation::AddPedAtAttractor (0x614210) from GeneratePedsAtAttractors
static bool __cdecl CPopulation__AddPedAtAttractor_Hook(int modelIndex, C2dEffect* pAttractor, CVector posn,
    CEntity* pEntity, int decisionMakerType)
{
    if (!CWorldGridSync::CanSpawnAt(posn))
    {
        return false;
    }

    return plugin::CallAndReturn<bool, 0x614210, int, C2dEffect*, CVector, CEntity*, int>(
        modelIndex, pAttractor, posn, pEntity, decisionMakerType);
}

// CCarGenerator::DoInternalProcessing (0x6F34D0): creates the parked vehicle of a generator
static void __fastcall CCarGenerator__DoInternalProcessing_Hook(CCarGenerator* This)
{
    CVector position(This->m_vecPosn.x / 8.0f, This->m_vecPosn.y / 8.0f, This->m_vecPosn.z / 8.0f);
    if (!CWorldGridSync::CanSpawnAt(position))
    {
        return;  // the generator tries again later
    }

    plugin::CallMethod<0x6F34D0, CCarGenerator*>(This);
}

// CHeli::UpdateHelis (0x6C79A0) from CGame::Process: every wanted player's game made its own 2 police helis (and the
// news one), the others got them as clones too. With a remote one of those near a new one waits: the next generation
// time (CHeli::TestForNewRandomHelisTimer, 0xC1C960; GenerateHeli's result is stored unchecked, 0x6C7ADD, so it
// can't be vetoed itself) goes on
static void __cdecl CHeli__UpdateHelis_Hook()
{
    constexpr float NEAR_DISTANCE = 400.0f;
    constexpr unsigned int WAIT = 1000;
    constexpr int MODEL_POLMAV = 497;
    constexpr int MODEL_VCNMAV = 488;  // the news heli

    CPlayerPed* pPlayer = FindPlayerPed(0);
    if (pPlayer)
    {
        for (CNetworkVehicle* pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
        {
            CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
            if (!pNetworkVehicle->m_bSyncing && pVehicle &&
                (pVehicle->m_nModelIndex == MODEL_POLMAV || pVehicle->m_nModelIndex == MODEL_VCNMAV) &&
                (pVehicle->GetPosition() - pPlayer->GetPosition()).Magnitude() < NEAR_DISTANCE)
            {
                unsigned int& nNextHeliTime = *reinterpret_cast<unsigned int*>(0xC1C960);
                nNextHeliTime = std::max(nNextHeliTime, CTimer::m_snTimeInMilliseconds + WAIT);
                break;
            }
        }
    }

    plugin::Call<0x6C79A0>();
}

// CPlane::DoPlaneGenerationAndRemoval (0x6CD2F0) from CGame::Process: the planes in the sky are the host's, as the
// world state; every game had its own, the others' as clones on top
static void __cdecl CPlane__DoPlaneGenerationAndRemoval_Hook()
{
    if (CLocalPlayer::m_bIsHost || !CNetwork::m_bAuthenticated)
    {
        plugin::Call<0x6CD2F0>();
    }
}

void CWorldGridSync::InjectHooks()
{
    // call sites, not the functions: they start with HOODLUM stubs
    patch::RedirectCall(0x430455, CCarCtrl__GenerateCarCreationCoors2_Hook);  // CCarCtrl::GenerateOneRandomCar
    patch::RedirectCall(0x42B86A, CCarCtrl__GenerateCarCreationCoors2_Hook);  // GenerateOneEmergencyServicesCar
    patch::RedirectCall(0x614B1C, CPathFind__GeneratePedCreationCoors_Hook);  // CPopulation::AddToPopulation
    patch::RedirectCall(0x615C0D, CPopulation__AddPedAtAttractor_Hook);      // GeneratePedsAtAttractors
    patch::RedirectCall(0x6F3EC1, CCarGenerator__DoInternalProcessing_Hook);  // CCarGenerator::Process
    patch::RedirectCall(0x53BFE2, CHeli__UpdateHelis_Hook);                    // CGame::Process
    patch::RedirectCall(0x434272, CPlane__DoPlaneGenerationAndRemoval_Hook);   // CGame::Process
}
