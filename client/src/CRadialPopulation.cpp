#include "stdafx.h"
#include "CRadialPopulation.h"

// "is it on screen" in the population decisions that only change a distance: always yes, so nothing is removed or
// rejected for being behind the camera. Fading out on screen and the spawn visibility checks stay
static bool __fastcall CEntity__GetIsOnScreen_Always_Hook(CEntity* This, int)
{
    return true;
}

void CRadialPopulation::InjectHooks()
{
    // after CCarPedGenHooks, it hooks the PossiblyRemoveVehicle and ManagePed call sites too

    // CCarCtrl::PossiblyRemoveVehicle: off screen the removal range was 45 instead of 170
    patch::RedirectCall(0x425057, CEntity__GetIsOnScreen_Always_Hook);

    // CCarCtrl::PossiblyRemoveVehicle: with the camera looking almost straight down (front z < -0.9, meant for
    // a helicopter) the removal range was 70 instead of 170, so a glance at the ground wiped the traffic around
    // the sector owner for every player. jp -> jmp, the range never shrinks
    patch::SetUChar(0x425109, 0xEB);

    // CCarCtrl::PossiblyRemoveVehicle: an off screen simple car (all distant traffic), or a physics one without
    // a mission, was removed as soon as it was 22 units away (2D). Looking at the ground for a moment wiped the
    // traffic around the sector owner, and every other player lost it with him
    patch::RedirectCall(0x4252B3, CEntity__GetIsOnScreen_Always_Hook);

    // CCarCtrl::PossiblyRemoveVehicle: an off screen wreck older than 60 s was removed at 42.25 units,
    // now it goes by the common removal range
    patch::RedirectCall(0x4253B1, CEntity__GetIsOnScreen_Always_Hook);

    // CPopulation::ManagePed: between 25 and 54.5 units a ped was removed as soon as it was off screen
    patch::RedirectCall(0x612157, CEntity__GetIsOnScreen_Always_Hook);

    // CCarCtrl::GenerateOneRandomCar: a new off screen car further than 45 units was deleted right away,
    // now it is checked like a visible one, i.e. must be on the 150..170 ring the generator uses
    patch::RedirectCall(0x43199F, CEntity__GetIsOnScreen_Always_Hook);
}
