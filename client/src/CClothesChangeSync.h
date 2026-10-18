#pragma once

// changing clothes (a clothes shop, the wardrobe at home): the shop scripts are local on every client, the player
// stands on the changing spot with a fixed camera. While one changes, the others don't see each other there: the
// changing player's clone is hidden (STATE_CHANGING_CLOTHES in the on foot updates) and while we change we hide every
// clone. The try-ons aren't sent, only the outfit we leave with.
class CClothesChangeSync
{
public:
    // the shop scripts set the minigame flag for the changing mode only (CLOTH, WARROBE)
    static bool IsLocalPlayerChanging();
    // the dress hook: true when the rebuild is held until we leave the changing mode
    static bool HoldRebuild();
    static void Process();

private:
    static bool ms_bChanging;
    static bool ms_bRebuildHeld;
};
