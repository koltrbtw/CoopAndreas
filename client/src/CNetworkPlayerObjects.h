#pragma once

// mission objects of one network player's own: the mission runs on the host and its objects aren't synced, so a task
// for everyone (the bottles of SWEET2) is only the host's. The host's script creates them for each player
// (Coop.CreateObjectForNetworkPlayer), that player's game creates and watches them, a broken one is reported back
// and the script reads the count (Coop.GetObjectsBrokenByNetworkPlayer)
class CNetworkPlayerObjects
{
public:
    // the player
    static void Add(int nModel, const CVector& pos, float fHeading);
    static void RemoveAll();
    // ours nearest to pos, as BREAK_OBJECT: the shot of a clone may miss it here, the script broke the host's
    static void Break(const CVector& pos);
    // every frame: our objects that were hit go, the host hears of each
    static void Process();

private:
    static std::vector<int> ms_handles;
};
