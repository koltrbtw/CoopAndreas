#include "stdafx.h"
#include "CPedAnimSync.h"
#include <CAnimManager.h>
#include <CAnimBlendAssociation.h>
#include <CAnimBlendAssocGroup.h>
#include <CAnimBlendHierarchy.h>
#include <CAnimBlock.h>

static constexpr int IFP_MODEL_BASE = 25575;          // streaming ids of the anim blocks (RESOURCE_ID_IFP)
static constexpr uint16_t ANIM_FLAG_LOOPED = 0x2;
static constexpr uint16_t ANIM_FLAG_BLEND_AUTO_REMOVE = 0x4;
static constexpr uint16_t ANIM_FLAG_PARTIAL = 0x10;
static constexpr uint16_t ANIM_FLAG_FACIAL = 0x8000;
static constexpr float MIN_BLEND_AMOUNT = 0.05f;
static constexpr float MAX_TIME_ERROR = 0.15f;         // seconds, the clone keeps its own time below that
static constexpr float FADE_OUT_DELTA = -4.0f;

// left to the clone itself: the face (speech plays it), the guns (CTaskSimpleUseGun from the aim sync) and the
// vehicles (enter / exit is warped, a get-in anim would play in the middle of the street)
static bool IsSyncedAnim(const CAnimBlendAssociation* pAssoc)
{
    if (pAssoc->m_nFlags & ANIM_FLAG_FACIAL)
    {
        return false;
    }

    int nGroup = pAssoc->m_nAnimGroup;
    bool bVehicle = (nGroup >= ANIM_GROUP_BIKES && nGroup <= ANIM_GROUP_QUAD) ||
                    (nGroup >= ANIM_GROUP_STDCARAMIMS && nGroup <= ANIM_GROUP_NVADAPLANEANIMS) ||
                    nGroup == ANIM_GROUP_DRIVEBYS || nGroup == ANIM_GROUP_BIKE_DBZ || nGroup == ANIM_GROUP_COP_DBZ ||
                    nGroup == ANIM_GROUP_QUAD_DBZ;
    bool bGun = nGroup >= ANIM_GROUP_PYTHON && nGroup <= ANIM_GROUP_GOGGLES;
    return !bVehicle && !bGun;
}

static CAnimBlendAssociation* FindAnim(RpClump* pClump, int nGroup, int nAnimId)
{
    for (auto pAssoc = RpAnimBlendClumpGetFirstAssociation(pClump); pAssoc; pAssoc = RpAnimBlendGetNextAssociation(pAssoc))
    {
        if (pAssoc->m_nAnimGroup == nGroup && pAssoc->m_nAnimId == nAnimId)
        {
            return pAssoc;
        }
    }
    return nullptr;
}

// the block of the group may not be loaded here (gangs, attractors...): request it, play the anim from a later packet
bool CPedAnimSync::IsGroupLoaded(int nGroup)
{
    // 0xB4EA34 holds a pointer to the groups, plugin-sdk's CAnimManager::ms_aAnimAssocGroups takes it for the array
    CAnimBlendAssocGroup* pGroups = *reinterpret_cast<CAnimBlendAssocGroup**>(0xB4EA34);
    CAnimBlendAssocGroup& group = pGroups[nGroup];
    if (group.m_pAssociations == nullptr)
    {
        return false;
    }

    CAnimBlock* pBlock = group.m_pAnimBlock;
    if (pBlock && !pBlock->bLoaded)
    {
        int nBlockIndex = static_cast<int>(pBlock - CAnimManager::ms_aAnimBlocks);
        CStreaming::RequestModel(IFP_MODEL_BASE + nBlockIndex, 0);
        return false;
    }
    return true;
}

int CPedAnimSync::Collect(CPed* pPed, SPedAnim* pAnims, int nMaxAnims, int nOnlyGroup)
{
    if (!pPed->m_pRwClump)
    {
        return 0;
    }

    std::vector<CAnimBlendAssociation*> anims;
    for (auto pAssoc = RpAnimBlendClumpGetFirstAssociation(pPed->m_pRwClump); pAssoc;
         pAssoc = RpAnimBlendGetNextAssociation(pAssoc))
    {
        if (pAssoc->m_fBlendAmount >= MIN_BLEND_AMOUNT && IsSyncedAnim(pAssoc) &&
            (nOnlyGroup < 0 || pAssoc->m_nAnimGroup == nOnlyGroup))
        {
            anims.push_back(pAssoc);
        }
    }

    // the most visible ones if there are more than fit
    std::sort(anims.begin(), anims.end(),
        [](auto a, auto b) { return a->m_fBlendAmount > b->m_fBlendAmount; });

    int nCount = std::min(static_cast<int>(anims.size()), nMaxAnims);
    for (int i = 0; i < nCount; i++)
    {
        CAnimBlendAssociation* pAssoc = anims[i];
        SPedAnim& anim = pAnims[i];
        anim.group = static_cast<uint8_t>(pAssoc->m_nAnimGroup);
        anim.id = static_cast<uint16_t>(pAssoc->m_nAnimId);
        anim.time = pAssoc->m_fCurrentTime;
        anim.blendAmount = pAssoc->m_fBlendAmount;
        anim.blendDelta = pAssoc->m_fBlendDelta;
        anim.speed = pAssoc->m_fSpeed;
    }
    return nCount;
}

// a full body anim that keeps playing: the clump needs at least one, RpAnimBlendClumpUpdateAnimations takes the
// blend nodes of the anims that still have weight and the frame update (0x4D1750) reads the first one without a check
static bool IsBaseAnim(const CAnimBlendAssociation* pAssoc)
{
    return !(pAssoc->m_nFlags & ANIM_FLAG_PARTIAL) && pAssoc->m_fBlendAmount > 0.0f && pAssoc->m_fBlendDelta >= 0.0f;
}

// the syncer's base anim may be one we don't sync (a gun, a vehicle) or still too light to be sent
static void EnsureBaseAnim(CPed* pPed)
{
    for (auto pAssoc = RpAnimBlendClumpGetFirstAssociation(pPed->m_pRwClump); pAssoc;
         pAssoc = RpAnimBlendGetNextAssociation(pAssoc))
    {
        if (IsBaseAnim(pAssoc))
        {
            return;
        }
    }
    CAnimManager::BlendAnimation(pPed->m_pRwClump, pPed->m_nAnimGroup, ANIM_DEFAULT_IDLE_STANCE, 8.0f);
}

void CPedAnimSync::Apply(CPed* pPed, const SPedAnim* pAnims, int nCount, int nOnlyGroup)
{
    // stabbed: our copy of the stealth kill plays the victim's anims (PED_STEALTH_KILL), the syncer's run a ping apart
    RpClump* pClump = pPed->m_pRwClump;
    if (!pClump || !pPed->IsAlive() || pPed->m_ePedState == PEDSTATE_DIE_BY_STEALTH)
    {
        return;
    }

    CAnimBlendAssociation* applied[Packets::Peds::PedOnFoot::MAX_ANIMS]{};
    for (int i = 0; i < nCount; i++)
    {
        const SPedAnim& anim = pAnims[i];
        if (anim.group >= CAnimManager::ms_numAnimAssocDefinitions)
        {
            continue;
        }

        CAnimBlendAssociation* pAssoc = FindAnim(pClump, anim.group, anim.id);
        if (!pAssoc)
        {
            if (!IsGroupLoaded(anim.group))
            {
                continue;
            }
            pAssoc = CAnimManager::AddAnimation(pClump, anim.group, anim.id);
            if (!pAssoc)
            {
                continue;
            }
            pAssoc->SetCurrentTime(anim.time);
        }
        else
        {
            float fTotalTime = pAssoc->m_pHierarchy->m_fTotalTime;
            float fError = std::fabs(pAssoc->m_fCurrentTime - anim.time);
            if (pAssoc->m_nFlags & ANIM_FLAG_LOOPED)
            {
                fError = std::min(fError, fTotalTime - fError);
            }
            if (fError > MAX_TIME_ERROR)
            {
                pAssoc->SetCurrentTime(std::clamp(anim.time, 0.0f, fTotalTime));
            }
        }

        // the syncer's blend, not the one the clone's own BlendAnimation calls gave it
        pAssoc->m_fBlendAmount = anim.blendAmount;
        pAssoc->m_fBlendDelta = anim.blendDelta;
        pAssoc->m_fSpeed = anim.speed;
        pAssoc->m_nFlags &= ~ANIM_FLAG_BLEND_AUTO_REMOVE;
        if (anim.blendDelta < 0.0f)
        {
            pAssoc->m_nFlags |= ANIM_FLAG_BLEND_AUTO_REMOVE;
        }
        applied[i] = pAssoc;
    }

    bool bSyncerHasBase = std::any_of(std::begin(applied), std::end(applied), [](auto a) { return a && IsBaseAnim(a); });

    // what the syncer doesn't play anymore (or never did, like the idle gestures the clone picks itself)
    for (auto pAssoc = RpAnimBlendClumpGetFirstAssociation(pClump); pAssoc; pAssoc = RpAnimBlendGetNextAssociation(pAssoc))
    {
        if (!IsSyncedAnim(pAssoc) || (nOnlyGroup >= 0 && pAssoc->m_nAnimGroup != nOnlyGroup) ||
            std::find(std::begin(applied), std::end(applied), pAssoc) != std::end(applied))
        {
            continue;
        }

        // the clone's own base anim stays until the syncer sends one
        if (!bSyncerHasBase && !(pAssoc->m_nFlags & ANIM_FLAG_PARTIAL))
        {
            continue;
        }

        if (pAssoc->m_fBlendDelta > FADE_OUT_DELTA)
        {
            pAssoc->m_fBlendDelta = FADE_OUT_DELTA;
        }
        pAssoc->m_nFlags |= ANIM_FLAG_BLEND_AUTO_REMOVE;
    }

    EnsureBaseAnim(pPed);
}

void CPedAnimSync::Release(CPed* pPed)
{
    RpClump* pClump = pPed->m_pRwClump;
    if (!pClump)
    {
        return;
    }

    // the full body ones are replaced by whatever the tasks blend in, a partial one would stay forever
    for (auto pAssoc = RpAnimBlendClumpGetFirstAssociation(pClump, ANIM_FLAG_PARTIAL); pAssoc;
         pAssoc = RpAnimBlendGetNextAssociation(pAssoc, ANIM_FLAG_PARTIAL))
    {
        if (IsSyncedAnim(pAssoc))
        {
            pAssoc->m_fBlendDelta = FADE_OUT_DELTA;
            pAssoc->m_nFlags |= ANIM_FLAG_BLEND_AUTO_REMOVE;
        }
    }

    // CPed::SetMoveAnim blends the move anims again only when the move state changes (field_538 is
    // m_nSwimmingMoveState, the last state it blended)
    pPed->field_538 = PEDMOVE_NONE;
}
