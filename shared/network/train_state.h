#pragma once

#include <cstdint>
#include "serialize.h"

// a train is one network vehicle, its front carriage. Every client builds the carriages itself with
// CTrain::CreateMissionTrain from the type, and the whole train follows from the position on the track
struct TrainState
{
    static constexpr int TRAIN_TYPE_COUNT = 16;  // the configs at 0x8D44F8
    static constexpr int TRACK_COUNT = 4;

    uint8_t type = 0;
    bool clockwise = false;
    int8_t trackId = 0;
    int32_t nodeIndex = 0;
    float railDistance = 0.0f;  // CTrain::m_fCurrentRailDistance of the front carriage
    float speed = 0.0f;         // CTrain::m_fTrainSpeed, signed along the track
    uint8_t cruiseSpeed = 0;    // the syncer's autopilot, the clone follows the speed instead
    bool stopsAtStations = false;
    bool derailed = false;  // CTrain::bNotOnARailRoad: the front carriage is a plain physical then

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, type, 0, TRAIN_TYPE_COUNT - 1);
        serialize_bool(stream, clockwise);
        serialize_int(stream, trackId, 0, TRACK_COUNT - 1);
        serialize_int(stream, nodeIndex, 0, 0x7FFF);
        serialize_float(stream, railDistance);
        serialize_float(stream, speed);
        serialize_uint8(stream, cruiseSpeed);
        serialize_bool(stream, stopsAtStations);
        serialize_bool(stream, derailed);
        return true;
    }
};
