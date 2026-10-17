#pragma once

// debug instances only (--coopd<N>): commands for trying the train sync on the stand without the window focus. A line
// in <game>\CoopAndreas\debug-cmd<N>.txt is run once and the file removed:
//   mission-train <type>        CREATE_MISSION_TRAIN at the nearest track node, as a script does
//   delete-mission-trains       DELETE_MISSION_TRAINS (also: release-mission-trains, delete-all-trains)
//   drive-train                 warp the player into the driver seat of the nearest train
//   train-speed <speed>         SET_TRAIN_SPEED of the train we drive (above 50 it derails in a curve)
class CDebugTrains
{
public:
    static void Process();
};
