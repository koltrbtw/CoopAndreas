#pragma once

#include <unordered_map>

// The world scripts no mission sync covers (the impound lot of a police station, the guards of an airport...) run on
// every client and each one created its own cops and cars at the same spots: a copy of each at every other player.
// Their CREATE_CHAR / CREATE_CAR gets the clone of the copy another player's script already made there, the way the
// shop scripts do it with Coop.GetPedInAreaWithModel, and their DELETE / MARK_AS_NO_LONGER_NEEDED of a clone only
// lets go of it: the entity is its syncer's (and our own one goes to a player near it). Not the missions: one player
// runs them
class CScriptEntityAdoption
{
public:
    enum class eResult
    {
        NOT_HANDLED,  // the game runs the command
        HANDLED,      // done: the script got a clone, or let go of one
        WAIT,         // the spot is another player's, its copy is on the way: the command again next frame
    };

    static eResult ProcessCommand(uint16_t command, CRunningScript* pScript);

private:
    static constexpr float PED_RADIUS = 1.5f;  // 2D, the game puts a ped a bit off and above the script's z
    static constexpr float VEHICLE_RADIUS = 3.0f;
    static constexpr float OCCUPANT_RADIUS = 6.0f;  // on foot by the vehicle it was put in
    static constexpr float MAX_HEIGHT = 3.0f;
    static constexpr unsigned int MAX_WAIT = 3000;  // ms for the clone; then the script makes its own one
    // our entity a script deletes or no longer needs goes to a player this near: its copy of the script has it as
    // a clone
    static constexpr float HANDOVER_RADIUS = 150.0f;

    struct SWait
    {
        const uint8_t* m_pIP = nullptr;  // the command it waits at
        unsigned int m_nStart = 0;
    };
    static inline std::unordered_map<const CRunningScript*, SWait> ms_waits;

    static bool IsScriptSynced(const CRunningScript* pScript);
    static bool IsClone(CPed* pPed);
    static bool IsClone(CVehicle* pVehicle);
    static int FindPed(int model, const CVector& position, float fRadius);
    static int FindVehicle(int model, const CVector& position);
    static int FindOccupant(CVehicle* pVehicle, int model, int seat);
    static bool IsOursToSpawn(const CVector& position);
    static eResult WaitOrCreate(CRunningScript* pScript, const uint8_t* pCommandIP, const CVector& position, int model);
};
