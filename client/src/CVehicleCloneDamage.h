#pragma once

// a clone takes no damage of its own (its tyres, its health): its syncer's game has the same hit and sends the
// result (VEHICLE_DAMAGE, the health in the updates). A tyre burst by a spike strip, a shot, an explosion here only
// was flat till the syncer's next damage change, the health of a parked one dropped till its next idle update (it
// burned and smoked)
class CVehicleCloneDamage
{
public:
    static void InjectHooks();
};
