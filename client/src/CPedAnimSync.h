#pragma once

// the animations of a network ped: the syncer sends what plays on its ped, the clone plays the same set. The move
// state alone gives only walking and standing, so gestures, chats and fights were lost and the clone slid on an idle
class CPedAnimSync
{
public:
    // returns the number written to pAnims
    // nOnlyGroup: just the anims of that group (a player's idle anims) and the named ones a script plays, the rest is
    // the clone's own
    static int Collect(CPed* pPed, SPedAnim* pAnims, int nMaxAnims, int nOnlyGroup = -1);
    static void Apply(CPed* pPed, const SPedAnim* pAnims, int nCount, int nOnlyGroup = -1);
    // the ped became ours: fade out what the packets kept on it, its own tasks play their anims now
    static void Release(CPed* pPed);
    // every frame: the anim blocks held for the clones go once no mission runs
    static void Process();
    // false: its anim block is still streaming, it is requested now
    static bool IsGroupLoaded(int nGroup);
    // the get in/sit/out anims of this vehicle (a quad, a bike: their own IFP) are loaded, requested if not. A clone
    // warped or put in a vehicle whose block isn't loaded here crashed in CAnimManager::BlendAnimation (0x4D464E)
    static bool AreVehicleAnimsLoaded(CVehicle* pVehicle);
};
