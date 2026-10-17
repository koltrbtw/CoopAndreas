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
    // after CCarPedGenHooks, it hooks the first two call sites too

    // CCarCtrl::PossiblyRemoveVehicle: off screen the removal range was 45 instead of 170
    patch::RedirectCall(0x425057, CEntity__GetIsOnScreen_Always_Hook);

    // CPopulation::ManagePed: between 25 and 54.5 units a ped was removed as soon as it was off screen
    patch::RedirectCall(0x612157, CEntity__GetIsOnScreen_Always_Hook);

    // CCarCtrl::GenerateOneRandomCar: a new off screen car further than 45 units was deleted right away,
    // now it is checked like a visible one, i.e. must be on the 150..170 ring the generator uses
    patch::RedirectCall(0x43199F, CEntity__GetIsOnScreen_Always_Hook);
}
