#pragma once

// an empty clone we hit (our player on foot, a vehicle we sync) moved here only, its syncer's next idle update put it
// back: a jump back, the syncer's car hardly moved. We ask for it (MTA's push sync, CClientGame.cpp) and go on from
// where it is here
class CVehiclePushSync
{
public:
    static void Process();
    // asked for it a moment ago: the syncer's idle updates are from before our hit, they would put it back
    static bool IsWaitingForHandoff(CNetworkVehicle* pNetworkVehicle);
};
