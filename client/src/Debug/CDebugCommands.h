#pragma once

#include <string>

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
//   gang-wars on|off            CGangWars::SetGangWarsActive, as GROVE2 does
//   god                         the player bullet/fire/explosion/melee proof
//   provoke                     enough provocation for CGangWars::Update to start an offensive war in this zone
//   shoot-wave [count]          headshots at up to count members of the war's gang closer than 100 m (synced shots)
//   objects                     log the map objects within 40 m (CWorldObjectSync): model, flags, static, displaced
//   object-damage <n>           CObject::ObjectDamage on the n nearest breakable map objects, hit by the player
//   object-push                 the nearest movable map object flies off, pushed by the player
//   status                      log the player's position, health, armour and wanted level
class CDebugCommands
{
public:
    static void Process();

private:
    static void RunCommand(const std::string& line);

    static inline bool ms_bPoliceOff = false;
};
