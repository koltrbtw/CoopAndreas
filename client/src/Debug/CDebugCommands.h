#pragma once

#include <string>
#include <vector>

// debug instances only (--coopd<N>): commands for the stand, run without the window focus. A line in
// <game>\CoopAndreas\debug-cmd<N>.txt is run once and the file removed (start-coop.ps1 writes it before the start, it
// waits there until we are in the game):
//   police off|on               off: no wanted level at all (the locked cities give 4 stars, cops kill the testers)
//   mission-train <type>        CREATE_MISSION_TRAIN at the nearest track node, as a script does
//   delete-mission-trains       DELETE_MISSION_TRAINS (also: release-mission-trains, delete-all-trains)
//   drive-train                 warp the player into the driver seat of the nearest train
//   train-speed <speed>         SET_TRAIN_SPEED of the train we drive (above 50 it derails in a curve)
//   script-speed <speed>        SET_TRAIN_SPEED / SET_TRAIN_CRUISE_SPEED of the nearest train, as a mission script
//   script-cruise <speed>
//   teleport <x> <y> <z>        the player (out of the vehicle), the scene loaded first
//   turn [degrees]              turn the camera around (180 by default)
//   pitch [radians]             the camera vertical angle (-1.4 by default, at the ground)
//   vehicles                    log the vehicles by the distance (22/45/100/170 units), total / on screen
//   gang-wars on|off            CGangWars::SetGangWarsActive, as GROVE2 does
//   forget-vehicle              our nearest vehicle deleted here without VEHICLE_REMOVE (a leaked clone for the others)
//   flash-vehicle [n]           n vehicles created and removed at once here, like a car generator (10 by default)
//   god                         the player bullet/fire/explosion/melee proof
//   provoke                     enough provocation for CGangWars::Update to start an offensive war in this zone
//   shoot-wave [count]          headshots at up to count members of the war's gang closer than 100 m (synced shots)
//   objects                     log the map objects within 40 m (CWorldObjectSync): model, flags, static, displaced
//   object-damage <n>           CObject::ObjectDamage on the n nearest breakable map objects, hit by the player
//   object-push                 the nearest movable map object flies off, pushed by the player
//   status                      log the player's position, health, armour and wanted level
//   car [model] [empty]         a parked car with a random driver (or empty) 6 m ahead (a jacking to test)
//   enter-driver                into the nearest vehicle as the driver, as the F key
//   enter-passenger             into the nearest network vehicle as a passenger, as the G key
//   warp-remote-passenger       warp the other players into seat 0 of the vehicle nearest to them (a late passenger update)
//   g                           press G for a frame, the passenger enter of CPassengerEnter
//   knock-off                   CEventKnockOffBike on the player in its vehicle, as a bike crash
//   leave [speed]               the exit key; with a speed the vehicle is pushed forward first (a roll out)
//   flush                       FlushImmediately on the player: out of the vehicle at once, as a script warp
//   jack-me                     a random ped next to our vehicle drags us out
//   ram-me [speed]              an empty car 12 m ahead rolls into the player (0.6 by default)
//   ram-ped [speed]             an empty car rolls into the nearest ped someone else syncs, watched (watch-ped)
//   watch-ped <id>              watch-anims logs that ped too (c1v12)
//   kill-me                     a pistol shot that kills the player (CWeapon::GenerateDamageEvent)
//   position-sync old|new       old: the remote player's position set as it comes (the baseline to compare with)
//   walk-to <dx> <dy> [run]     the player walks (runs) straight by that much (TASK_GO_STRAIGHT_TO_COORD)
//   watch-moves [ms]            per frame moves of the remote player's clone: mean, max, jerks, snaps, distance to
//                               the vehicle it stands on
//   ride [model]                a wandering car with an npc driver (455 flatbed), the player on its roof
//   burn-me                     set the player on fire (CFireManager::StartFire)
//   gas-me                      the player chokes (CTaskSimpleChoking, as tear gas)
//   climb                       turn round to a handhold near and climb it
//   npc-leave [speed]           the npc driver we sync of the nearest vehicle leaves it (pushed forward: rolls out)
//   npc-knock                   CEventKnockOffBike on that driver
//   npc-flush                   FlushImmediately on that driver: out at once
//   knife                       give the player a knife and take it in hand
//   stab [clone]                behind the nearest ped (one someone else syncs with "clone"), a stealth kill
//   watch-anims [ms]            log the active task and the anims of the players, the doors of the nearest car
class CDebugCommands
{
public:
    static void Process();

private:
    static void RunCommand(const std::string& line);

    static inline bool ms_bPoliceOff = false;
    static inline unsigned int ms_nWatchAnimsUntil = 0;
    static inline bool ms_bPressG = false;
    static inline unsigned int ms_nWatchMovesUntil = 0;
    static inline std::vector<std::pair<float, float>> ms_moves;  // a frame's move, the distance to the vehicle
    static inline CVector ms_vecLastMove{};
    static inline bool ms_bHasLastMove = false;
    static inline unsigned int ms_nSnapsAtStart = 0;
    static inline CVector ms_vecLastVehicle{};
    static inline float ms_fVehicleMoves = 0.0f;
};
