#pragma once

// the health bar of the mission cars (SWEET4's CAR HEALTH) for one player: the host's script shows it to every player
// (Coop.ShowMissionCarHealthBar), each game shows the health of the one of those cars its player sits in. The value
// goes into a global of main.scm (the counter's variable), unused here: the mission runs on the host only
class CMissionCarHealthBar
{
public:
    static void Set(uint16_t nVarOffset, const char* szGxt, vehicleid_t car1, vehicleid_t car2);
    static void Clear();
    static void Process();

private:
    static void Hide();

    static inline bool ms_bActive = false;
    static inline bool ms_bShown = false;
    static inline uint16_t ms_nVarOffset = 0;
    static inline char ms_szGxt[8]{};
    static inline vehicleid_t ms_cars[2]{};
    static inline float ms_fFullHealth[2]{1000.0f, 1000.0f};  // the most health each car had
};
