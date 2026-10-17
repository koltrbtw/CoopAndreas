#pragma once
#include <CVehicle.h>
#include "network/train_state.h"

namespace Packets::Vehicles
{
class VehicleSpawn : public Packet
{
    DEFINE_PACKET_TYPE(VehicleSpawn, ePacketType::VEHICLE_SPAWN, ePacketChannel::EVENT);

public:
    vehicleid_t vehicleid{};
    uint16_t modelid{};
    WorldPositionCompressed pos{};
    RadianAngleCompressed rot{};
    uint8_t color1 = 0;
    uint8_t color2 = 0;
    eVehicleCreatedBy createdBy = RANDOM_VEHICLE;
    // CVehicle::m_anExtras, the model's extra1..6 parts (roof, bed cover) the game picks at random, -1 is none
    int8_t extras[2] = {-1, -1};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_int(stream, modelid, MODEL_LANDSTAL, MODEL_UTILTR1);
        serialize_object(stream, pos);
        serialize_object(stream, rot);
        serialize_uint8(stream, color1);
        serialize_uint8(stream, color2);
        serialize_int(stream, (int&)createdBy, RANDOM_VEHICLE, PERMANENT_VEHICLE);
        serialize_int(stream, extras[0], -1, 5);
        serialize_int(stream, extras[1], -1, 5);
        return true;
    }
};
class VehicleRemove : public Packet
{
    DEFINE_PACKET_TYPE(VehicleRemove, ePacketType::VEHICLE_REMOVE, ePacketChannel::EVENT);

public:
    vehicleid_t vehicleid{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        return true;
    }
};

class VehicleIdleUpdate : public Packet
{
    DEFINE_PACKET_TYPE(VehicleIdleUpdate, ePacketType::VEHICLE_IDLE_UPDATE, ePacketChannel::SYNC);

public:
    vehicleid_t vehicleid{};

    WorldPositionCompressed pos{};
    NormalizedVector rot{};
    NormalizedVector roll{};
    MoveSpeedCompressed velocity{};
    CVector turnSpeed{};

    uint8_t color1{};
    uint8_t color2{};
    int8_t paintjob{};

    float health{};

    float planeGearState{};
    eDoorLock locked{};

    SVehicleExtras extras{};
    server_time_t sendTime = 0;  // g_serverTime of the sender: the receiver moves the target on by the delay
    bool hasRotorSpeed = false;  // helis, planes, boats: the clone's own ai doesn't spin them
    float rotorSpeed = 0.0f;

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);   

        serialize_object(stream, pos);
        serialize_object(stream, rot);
        serialize_object(stream, roll);
        serialize_object(stream, velocity);
        if (Stream::IsWriting)
        {
            turnSpeed.x = std::clamp(turnSpeed.x, -0.5f, 0.5f);
            turnSpeed.y = std::clamp(turnSpeed.y, -0.5f, 0.5f);
            turnSpeed.z = std::clamp(turnSpeed.z, -0.5f, 0.5f);
        }

        bool sendTurnSpeed = false;
        if (Stream::IsWriting)
        {
            if (std::abs(turnSpeed.x) >= 0.0001f || std::abs(turnSpeed.y) >= 0.0001f ||
                std::abs(turnSpeed.z) >= 0.0001f)
            {
                sendTurnSpeed = true;
            }
        }
        serialize_bool(stream, sendTurnSpeed);
        if (sendTurnSpeed)
        {
            serialize_compressed_float(stream, turnSpeed.x, -0.5f, 0.5f, 0.0001f);
            serialize_compressed_float(stream, turnSpeed.y, -0.5f, 0.5f, 0.0001f);
            serialize_compressed_float(stream, turnSpeed.z, -0.5f, 0.5f, 0.0001f);
        }
        else if (Stream::IsReading)
        {
            turnSpeed = CVector(0.0f, 0.0f, 0.0f);  // the packet object is reused, see SVehicleExtras
        }

        serialize_uint8(stream, color1);
        serialize_uint8(stream, color2);

        if (Stream::IsWriting)
        {
            health = std::clamp(health, 0.0f, MAX_VEHICLE_HEALTH);
        }
        serialize_compressed_float(stream, health, 0.0f, MAX_VEHICLE_HEALTH, 1.0f);

        if (Stream::IsWriting)
        {
            if (paintjob < -1 || paintjob > 2)  // got limits here https://wiki.multitheftauto.com/wiki/Paintjob
            {
                paintjob = -1;
            }
        }

        serialize_int(stream, paintjob, -1, 2);

        if (Stream::IsWriting)
        {
            bool temp = planeGearState > 0.0f;
            serialize_bool(stream, temp);
        }
        else if (Stream::IsReading)
        {
            bool temp;
            serialize_bool(stream, temp);
            planeGearState = temp ? 1.0f : 0.0f;
        }

        serialize_int(stream, (int&)locked, DOORLOCK_NOT_USED, DOORLOCK_SKIP_SHUT_DOORS);

        serialize_object(stream, extras);
        serialize_uint32(stream, sendTime);
        serialize_bool(stream, hasRotorSpeed);
        if (hasRotorSpeed)
        {
            serialize_float(stream, rotorSpeed);
        }

        return true;
    }
};

class VehicleDriverUpdate : public Packet
{
    DEFINE_PACKET_TYPE(VehicleDriverUpdate, ePacketType::VEHICLE_DRIVER_UPDATE, ePacketChannel::SYNC);

public:
    SenderPlayerId senderPlayerid{};
    vehicleid_t vehicleid{};
    eVehicleType vehicleSubType = VEHICLE_AUTOMOBILE;

    WorldPositionCompressed pos{};
    NormalizedVector rot{};
    NormalizedVector roll{};
    MoveSpeedCompressed velocity{};
    CVector turnSpeed{};

    uint8_t color1{};
    uint8_t color2{};
    int8_t paintjob{};

    Packets::Players::SHealthSnapshot playerHealth{};
    Packets::Players::SWeaponSnapshot playerWeapon{};
    Packets::Players::SKeySnapshot playerKeys{};

    float health{};

    float bikeLean{};
    unsigned short miscComponentAngle{};  // hydra thrusters

    float planeGearState{};
    eDoorLock locked{};
    vehicleid_t trailerid{};  // the vehicle it tows, a trailer or a car behind a tow truck
    int8_t radioStation = -1;  // eRadioID the driver listens to, for the passengers

    SVehicleExtras extras{};
    server_time_t sendTime = 0;  // g_serverTime of the sender: the receiver moves the target on by the delay
    bool hasRotorSpeed = false;  // helis, planes, boats: the clone's own ai doesn't spin them
    float rotorSpeed = 0.0f;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_vehicleid(stream, vehicleid);
        serialize_object(stream, pos);
        serialize_object(stream, rot);
        serialize_object(stream, roll);
        serialize_object(stream, velocity);
        if (Stream::IsWriting)
        {
            turnSpeed.x = std::clamp(turnSpeed.x, -0.5f, 0.5f);
            turnSpeed.y = std::clamp(turnSpeed.y, -0.5f, 0.5f);
            turnSpeed.z = std::clamp(turnSpeed.z, -0.5f, 0.5f);
        }

        bool sendTurnSpeed = false;
        if (Stream::IsWriting)
        {
            if (std::abs(turnSpeed.x) >= 0.0001f || std::abs(turnSpeed.y) >= 0.0001f ||
                std::abs(turnSpeed.z) >= 0.0001f)
            {
                sendTurnSpeed = true;
            }
        }
        serialize_bool(stream, sendTurnSpeed);

        if (sendTurnSpeed)
        {
            serialize_compressed_float(stream, turnSpeed.x, -0.5f, 0.5f, 0.0001f);
            serialize_compressed_float(stream, turnSpeed.y, -0.5f, 0.5f, 0.0001f);
            serialize_compressed_float(stream, turnSpeed.z, -0.5f, 0.5f, 0.0001f);
        }
        else if (Stream::IsReading)
        {
            turnSpeed = CVector(0.0f, 0.0f, 0.0f);  // the packet object is reused, see SVehicleExtras
        }

        serialize_uint8(stream, color1);
        serialize_uint8(stream, color2);

        if (Stream::IsWriting)
        {
            health = std::clamp(health, 0.0f, MAX_VEHICLE_HEALTH);
        }
        serialize_compressed_float(stream, health, 0.0f, MAX_VEHICLE_HEALTH, 1.0f);

        serialize_object(stream, playerHealth);
        serialize_object(stream, playerWeapon);
        serialize_object(stream, playerKeys);

        if (Stream::IsWriting)
        {
            if (paintjob < -1 || paintjob > 2)  // got limits here https://wiki.multitheftauto.com/wiki/Paintjob
            {
                paintjob = -1;
            }
        }

        serialize_int(stream, paintjob, -1, 2);

        if (Stream::IsWriting)
        {
            bikeLean = std::clamp(bikeLean, -1.0f, 1.0f);
        }
        serialize_compressed_float(stream, bikeLean, -1.0f, 1.0f, 0.01f);

        bool syncAngle = false;
        if (Stream::IsWriting && miscComponentAngle != 0)
        {
            syncAngle = true;
        }
        serialize_bool(stream, syncAngle);
        if (syncAngle)
        {
            serialize_uint16(stream, miscComponentAngle);
        }
        else if (Stream::IsReading)
        {
            miscComponentAngle = 0;
        }

        if (Stream::IsWriting)
        {
            bool temp = planeGearState > 0.0f;
            serialize_bool(stream, temp);
        }
        else if (Stream::IsReading)
        {
            bool temp;
            serialize_bool(stream, temp);
            planeGearState = temp ? 1.0f : 0.0f;
        }

        serialize_int(stream, (int&)locked, DOORLOCK_NOT_USED, DOORLOCK_SKIP_SHUT_DOORS);

        bool hasTrailer = Stream::IsWriting && trailerid.valid();
        serialize_bool(stream, hasTrailer);
        if (hasTrailer)
        {
            serialize_vehicleid(stream, trailerid);
        }
        else if (Stream::IsReading)
        {
            trailerid = vehicleid_t();  // unhooked: the reused packet object kept the last one
        }

        serialize_int(stream, radioStation, -1, 13);

        serialize_object(stream, extras);
        serialize_uint32(stream, sendTime);
        serialize_bool(stream, hasRotorSpeed);
        if (hasRotorSpeed)
        {
            serialize_float(stream, rotorSpeed);
        }

        return true;
    }
};

class VehicleEnter : public Packet
{
    DEFINE_PACKET_TYPE(VehicleEnter, ePacketType::VEHICLE_ENTER, ePacketChannel::EVENT);

public:
    SenderPlayerId senderPlayerid{};
    vehicleid_t vehicleid{};
    int8_t seatid = 0;  // 3bits
    bool bForce = false;
    bool bPassenger = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_vehicleid(stream, vehicleid);
        serialize_int(stream, seatid, -1, 7);  // TODO test properly
        serialize_bool(stream, bForce);
        serialize_bool(stream, bPassenger);

        return true;
    }
};

class VehicleExit : public Packet
{
    DEFINE_PACKET_TYPE(VehicleExit, ePacketType::VEHICLE_EXIT, ePacketChannel::EVENT);

public:
    SenderPlayerId senderPlayerid{};
    bool bForce = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_bool(stream, bForce);

        return true;
    }
};

class VehicleDamage : public Packet
{
    DEFINE_PACKET_TYPE(VehicleDamage, ePacketType::VEHICLE_DAMAGE, ePacketChannel::EVENT);

public:
    vehicleid_t vehicleid{};
    CDamageManager damageManager{};  // CAutomobile and the classes built on it
    uint8_t bikeWheelStatus[2]{};    // CBike::m_anWheelDamageState, eCarWheelStatus: front, rear

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_object(stream, damageManager);
        for (uint8_t& status : bikeWheelStatus)
        {
            serialize_int(stream, status, 0, 2);
        }
        return true;
    }
};

class VehicleComponentAdd : public Packet
{
    DEFINE_PACKET_TYPE(VehicleComponentAdd, ePacketType::VEHICLE_COMPONENT_ADD, ePacketChannel::EVENT);

public:
    vehicleid_t vehicleid{};
    int componentid{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_int(stream, componentid, 0, 19999);

        return true;
    }
};

class VehicleComponentRemove : public Packet
{
    DEFINE_PACKET_TYPE(VehicleComponentRemove, ePacketType::VEHICLE_COMPONENT_REMOVE, ePacketChannel::EVENT);

public:
    vehicleid_t vehicleid{};
    int componentid{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_int(stream, componentid, 0, 19999);

        return true;
    }
};

class VehiclePassengerUpdate : public Packet
{
    DEFINE_PACKET_TYPE(VehiclePassengerUpdate, ePacketType::VEHICLE_PASSENGER_UPDATE, ePacketChannel::SYNC);
   
public:
    SenderPlayerId senderPlayerid{};
    vehicleid_t vehicleid{};
    Packets::Players::SHealthSnapshot playerHealth{};
    Packets::Players::SWeaponSnapshot playerWeapon{};
    Packets::Players::SKeySnapshot playerKeys{};
    bool driveby;
    int8_t seatid;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_vehicleid(stream, vehicleid);
        serialize_object(stream, playerHealth);
        serialize_object(stream, playerWeapon);
        serialize_object(stream, playerKeys);
        serialize_bool(stream, driveby);
        serialize_int(stream, seatid, -1, 7);  // TODO test properly TODO(v0.3.1-alpha): limits

        return true;
    }
};

class AssignVehicleSyncer : public Packet
{
    DEFINE_PACKET_TYPE(AssignVehicleSyncer, ePacketType::ASSIGN_VEHICLE, ePacketChannel::EVENT);

public:
    vehicleid_t vehicleid{};
    bool isSyncer = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_bool(stream, isSyncer);

        return true;
    }
};

// client -> server: "give me this vehicle"; server -> current syncer: "release it to whoever asked"
class VehicleRequestOwnership : public Packet
{
    DEFINE_PACKET_TYPE(VehicleRequestOwnership, ePacketType::VEHICLE_REQUEST_OWNERSHIP, ePacketChannel::EVENT);

public:
    vehicleid_t vehicleid{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        return true;
    }
};

// old syncer -> server -> new syncer: the vehicle state the new syncer needs to continue simulating it.
// Npc peds inside that the old syncer owned move together with the vehicle
class VehicleHandoff : public Packet
{
    DEFINE_PACKET_TYPE(VehicleHandoff, ePacketType::VEHICLE_HANDOFF, ePacketChannel::EVENT);

public:
    static constexpr int MAX_OCCUPANTS = 9;  // driver + 8 passengers

    struct Occupant
    {
        pedid_t pedid{};
        int8_t seatid = -1;  // -1 is the driver
    };

    vehicleid_t vehicleid{};
    bool released = false;  // false: the syncer refused, nothing changes
    playerid_t target{};    // the syncer hands it over on its own (not asked): to whom
    bool hasTransform = true;  // false when the server hands it over, the new syncer keeps its clone transform

    WorldPositionCompressed pos{};
    NormalizedVector rot{};
    NormalizedVector roll{};
    MoveSpeedCompressed velocity{};

    uint8_t carMission{};
    uint8_t drivingStyle{};
    uint8_t cruiseSpeed{};

    uint8_t occupantCount{};
    Occupant occupants[MAX_OCCUPANTS]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_bool(stream, released);
        if (!released)
        {
            return true;
        }

        serialize_playerid(stream, target);
        serialize_bool(stream, hasTransform);
        if (hasTransform)
        {
            serialize_object(stream, pos);
            serialize_object(stream, rot);
            serialize_object(stream, roll);
            serialize_object(stream, velocity);
        }

        serialize_uint8(stream, carMission);
        serialize_uint8(stream, drivingStyle);
        serialize_uint8(stream, cruiseSpeed);

        serialize_int(stream, occupantCount, 0, MAX_OCCUPANTS);
        for (int i = 0; i < occupantCount; i++)
        {
            serialize_pedid(stream, occupants[i].pedid);
            serialize_int(stream, occupants[i].seatid, -1, 7);
        }
        return true;
    }
};

class SetVehicleCreatedBy : public Packet
{
    DEFINE_PACKET_TYPE(SetVehicleCreatedBy, ePacketType::SET_VEHICLE_CREATED_BY, ePacketChannel::EVENT);

public:
    vehicleid_t vehicleid{};
    eVehicleCreatedBy createdBy{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_int(stream, (int&)createdBy, RANDOM_VEHICLE, PERMANENT_VEHICLE);

        return true;
    }
};
// instead of VEHICLE_SPAWN for a train: the receiver builds all its carriages, the id is the front carriage's
class TrainSpawn : public Packet
{
    DEFINE_PACKET_TYPE(TrainSpawn, ePacketType::TRAIN_SPAWN, ePacketChannel::EVENT);

public:
    vehicleid_t vehicleid{};
    WorldPositionCompressed pos{};
    bool isMission = false;
    TrainState state{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_object(stream, pos);
        serialize_bool(stream, isMission);
        serialize_object(stream, state);
        return true;
    }
};

// instead of VEHICLE_IDLE_UPDATE for a train, from its syncer
class TrainUpdate : public Packet
{
    DEFINE_PACKET_TYPE(TrainUpdate, ePacketType::TRAIN_UPDATE, ePacketChannel::SYNC);

public:
    static constexpr int MAX_CARRIAGES = 15;  // a train config holds up to 15 models

    // off the rails every carriage moves by physics, not by the track position
    struct Carriage
    {
        WorldPositionCompressed pos{};
        NormalizedVector rot{};
        NormalizedVector roll{};
        MoveSpeedCompressed velocity{};

        template <typename Stream>
        bool Serialize(Stream& stream)
        {
            serialize_object(stream, pos);
            serialize_object(stream, rot);
            serialize_object(stream, roll);
            serialize_object(stream, velocity);
            return true;
        }
    };

    vehicleid_t vehicleid{};
    WorldPositionCompressed pos{};
    TrainState state{};

    // from the front carriage on, only when derailed
    uint8_t carriageCount{};
    Carriage carriages[MAX_CARRIAGES]{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_vehicleid(stream, vehicleid);
        serialize_object(stream, pos);
        serialize_object(stream, state);
        if (state.derailed)
        {
            serialize_int(stream, carriageCount, 0, MAX_CARRIAGES);
            for (int i = 0; i < carriageCount; i++)
            {
                serialize_object(stream, carriages[i]);
            }
        }
        return true;
    }
};
}  // namespace Packets::Vehicles
