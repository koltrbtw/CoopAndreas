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
    ms_nOwnedArea = pPacket->area;

    logger::info("world grid: we spawn the population in %d sectors of area %d", pPacket->count, pPacket->area);
}

void CWorldGridSync::Reset()
{
    ms_ownedSectors.reset();
    ms_nOwnedArea = 0;
}

bool CWorldGridSync::CanSpawnAt(const CVector& position)
{
    // offline or alone: the vanilla rules; the filling of an interior we enter alone (its attractors)
    if (!CNetwork::m_bAuthenticated || CNetworkPlayerManager::m_pPlayers.empty() || ms_bPopulatingInterior)
    {
        return true;
    }

    // an interior has sectors of its own (the vanilla rules there made two players in one interior spawn its
    // scenario peds twice); until the server hears of the new area, the sectors are another area's
    return CGame::currArea == ms_nOwnedArea && ms_ownedSectors.test(WorldGrid::GetSector(position.x, position.y));
}

bool CWorldGridSync::CanPopulateInterior(const CVector& position)
{
    // the interior is filled on the way in, before the server has the new area: its peds are of the player who is
    // already inside, they come to us as clones
    for (CNetworkPlayer* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        CPlayerPed* pPed = pNetworkPlayer->m_pPed;
        if (pPed && pPed->m_nAreaCode == CGame::currArea &&
            DistanceBetweenPoints(pPed->GetPosition(), position) < INTERIOR_RADIUS)
        {
            return false;
        }
    }
    return true;
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

// CPopulation::AddToPopulation (0x614720) from CPopulation::Update: inside an interior the peds don't come from
// GeneratePedCreationCoors; an interior is small, the sector of the player decides
static bool __cdecl CPopulation__AddToPopulation_Hook(float minDist, float maxDist, float minDistOffScreen,
    float maxDistOffScreen)
{
    if (CGame::currArea != 0 && !CWorldGridSync::CanSpawnAt(FindPlayerCoors(0)))
    {
        return false;
    }

    return plugin::CallAndReturn<bool, 0x614720, float, float, float, float>(
        minDist, maxDist, minDistOffScreen, maxDistOffScreen);
}

// CPopulation::PopulateInterior (0x616470) from CEntryExit (0x43E983): the peds of an interior (the cops of a police
// station) on the way in, at its attractors and its own spawn points, none of the generators above
static void __cdecl CPopulation__PopulateInterior_Hook(int numPeds, CVector posn)
{
    if (!CWorldGridSync::CanPopulateInterior(posn))
    {
        return;
    }

    CWorldGridSync::ms_bPopulatingInterior = true;
    plugin::Call<0x616470, int, CVector>(numPeds, posn);
    CWorldGridSync::ms_bPopulatingInterior = false;
}

// CPopulation::AddPed (0x612710) from the setup of the peds of an interior group (InteriorGroup_c: the staff and the
// customers of a shop). The group stores the ped it got and handles nullptr (no ped there)
static CPed* __cdecl CPopulation__AddPed_InteriorGroup_Hook(ePedType pedType, unsigned int modelIndex,
    const CVector& posn, bool bMakeWander)
{
    if (!CWorldGridSync::CanPopulateInterior(posn))
    {
        return nullptr;
    }

    return plugin::CallAndReturn<CPed*, 0x612710, ePedType, unsigned int, const CVector&, bool>(
        pedType, modelIndex, posn, bMakeWander);
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
    patch::RedirectCall(0x43E983, CPopulation__PopulateInterior_Hook);        // CEntryExit
    patch::RedirectCall(0x61679B, CPopulation__AddToPopulation_Hook);         // CPopulation::Update
    patch::RedirectCall(0x615D4B, CPopulation__AddToPopulation_Hook);
    patch::RedirectCall(0x594D48, CPopulation__AddPed_InteriorGroup_Hook);   // the peds of an interior group
    patch::RedirectCall(0x594FD7, CPopulation__AddPed_InteriorGroup_Hook);
    patch::RedirectCall(0x59676F, CPopulation__AddPed_InteriorGroup_Hook);
    patch::RedirectCall(0x6F3EC1, CCarGenerator__DoInternalProcessing_Hook);  // CCarGenerator::Process
    patch::RedirectCall(0x53BFE2, CHeli__UpdateHelis_Hook);                    // CGame::Process
    patch::RedirectCall(0x434272, CPlane__DoPlaneGenerationAndRemoval_Hook);   // CGame::Process
}
