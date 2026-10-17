#pragma once

// a train is one network vehicle, its front carriage (CNetworkVehicle::m_bTrain). The carriages behind it have no
// network id: every client builds them itself with CTrain::CreateMissionTrain from the train type, and the whole
// train follows from the position on the track (TrainState). Clones are mission trains for the local game, so
// DoTrainGenerationAndRemoval neither removes them nor counts them as its random train
class CTrainSync
{
public:
    // CTrainSync builds the train: the driver CreateMissionTrain puts in is a local npc of every client
    static inline bool ms_bCreatingTrain = false;

    // every 150 ms, with the idle vehicles: the state of ours, registering the ones not yet known
    static void Process();

    static void HandleSpawn(Packets::Vehicles::TrainSpawn* pPacket);
    static void HandleUpdate(Packets::Vehicles::TrainUpdate* pPacket);

    static void OnBecomeSyncer(CNetworkVehicle* pNetworkVehicle);
    static void OnBecomeClone(CNetworkVehicle* pNetworkVehicle);
    static void RemoveCloneTrain(CTrain* pFrontCarriage);

    // a train of ours: one network vehicle for all its carriages, TRAIN_SPAWN to the others
    static void RegisterHosted(CTrain* pFrontCarriage);

    static void InjectHooks();

private:
    static int FindTrainType(CTrain* pFrontCarriage);
    static void CollectState(CNetworkVehicle* pNetworkVehicle, TrainState& state);
    static void KeepClone(CNetworkVehicle* pNetworkVehicle);
    static void Derail(CTrain* pFrontCarriage);
};
