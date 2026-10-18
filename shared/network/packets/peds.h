#pragma once
#include <eModelID.h>
#include <ePedType.h>
#include <CPed.h>
#include <CVehicle.h>
#include <eGlobalSpeechContexts.h>
#include "network/packets/vehicles.h"  // eVehicleExitReason, eVehicleExitFall

namespace Packets::Peds
{
class PedSpawn : public Packet
{
    DEFINE_PACKET_TYPE(PedSpawn, ePacketType::PED_SPAWN, ePacketChannel::EVENT);

public:
    pedid_t pedid{};
    eModelID modelId = MODEL_MALE01;
    ePedType pedType = PED_TYPE_CIVMALE;
    WorldPositionCompressed pos{};
    eCharCreatedBy createdBy = MISSION_CHAR;
    char specialModelName[8] = {'\0'};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_int(stream, (int&)modelId, MODEL_NULL, MODEL_SPECIAL10);
        serialize_int(stream, (int&)pedType, PED_TYPE_PLAYER1, PED_TYPE_MISSION8);
        serialize_object(stream, pos);
        serialize_int(stream, (int&)createdBy, UNUSED_CHAR, REPLAY_CHAR);
        serialize_string(stream, specialModelName, 8);
        specialModelName[ARRAY_SIZE(specialModelName) - 1] = '\0';
        return true;
    }
};

class PedRemove : public Packet
{
    DEFINE_PACKET_TYPE(PedRemove, ePacketType::PED_REMOVE, ePacketChannel::EVENT);

public:
    pedid_t pedid{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        return true;
    }
};

class AssignPedSyncer : public Packet
{
    DEFINE_PACKET_TYPE(AssignPedSyncer, ePacketType::ASSIGN_PED, ePacketChannel::EVENT);

public:
    pedid_t pedid{};
    bool isSyncer = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_bool(stream, isSyncer);
        return true;
    }
};

// client -> server: "give me this ped"; server -> current syncer: "release it to whoever asked"
class PedRequestOwnership : public Packet
{
    DEFINE_PACKET_TYPE(PedRequestOwnership, ePacketType::PED_REQUEST_OWNERSHIP, ePacketChannel::EVENT);

public:
    pedid_t pedid{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        return true;
    }
};

// old syncer -> server -> new syncer: the on foot ped state the new syncer needs to continue simulating it
class PedHandoff : public Packet
{
    DEFINE_PACKET_TYPE(PedHandoff, ePacketType::PED_HANDOFF, ePacketChannel::EVENT);

public:
    pedid_t pedid{};
    bool released = false;  // false: the syncer refused, nothing changes
    playerid_t target{};    // the syncer hands it over on its own (not asked): to whom
    bool hasTransform = true;  // false when the server hands it over, the new syncer keeps its clone transform

    WorldPositionCompressed pos{};
    float heading{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
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
            serialize_float(stream, heading);
        }
        return true;
    }
};

using ::SPedAnim;  // serializable_types.h, the player's idle anims use it too

class PedOnFoot : public Packet
{
    DEFINE_PACKET_TYPE(PedOnFoot, ePacketType::PED_ONFOOT, ePacketChannel::SYNC);

public:
    static constexpr int MAX_ANIMS = 6;

    pedid_t pedid{};
    WorldPositionCompressed pos{};
    MoveSpeedCompressed velocity{};
    Packets::Players::SHealthSnapshot healthSnapshot{};
    Packets::Players::SWeaponSnapshot weaponSnapshot{};
    RadianAngleCompressed aimingRotation{};
    RadianAngleCompressed currentRotation{};
    RadianAngleCompressed lookDirection{};  // this one isnt needed i think
    eMoveState moveState = PEDMOVE_STILL;
    bool bDucked = false;
    bool bAiming = false;
    uint8_t fightingStyle = 4;
    WorldPositionCompressed weaponAim{};
    int animCount = 0;
    SPedAnim anims[MAX_ANIMS]{};
    uint8_t stateFlags = 0;  // CPedStateSync: on fire, swimming, choking, climbing

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_object(stream, pos);
        serialize_object(stream, velocity);
        serialize_object(stream, healthSnapshot);
        serialize_object(stream, weaponSnapshot);
        serialize_object(stream, aimingRotation);
        serialize_object(stream, currentRotation);
        serialize_object(stream, lookDirection);
        serialize_int(stream, (int&)moveState, PEDMOVE_NONE, PEDMOVE_SPRINT);
        serialize_bool(stream, bDucked);
        serialize_bool(stream, bAiming);
        serialize_uint8(stream, fightingStyle);  // todo
        if (bAiming)
        {
            serialize_object(stream, weaponAim);
        }
        serialize_int(stream, animCount, 0, MAX_ANIMS);
        for (int i = 0; i < animCount; i++)
        {
            serialize_object(stream, anims[i]);
        }
        serialize_uint8(stream, stateFlags);
        return true;
    }
};

class PedDriverUpdate : public Packet
{
    DEFINE_PACKET_TYPE(PedDriverUpdate, ePacketType::PED_DRIVER_UPDATE, ePacketChannel::SYNC);

public:
    pedid_t pedid{};
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

    Packets::Players::SHealthSnapshot pedHealth{};
    Packets::Players::SWeaponSnapshot pedWeapon{};

    float health{};

    eDoorLock locked = DOORLOCK_UNLOCKED;
    vehicleid_t trailerid{};  // the vehicle it tows, a trailer or a car behind a tow truck
    float gasPedal = 0.0f;
    float breakPedal = 0.0f;
    float steerAngle = 0.0f;

    bool siren = false;      // CVehicle::m_nVehicleFlags.bSirenOrAlarm
    uint8_t hornCounter{};   // CVehicle::m_nHornCounter, frames left to honk

    // subType specific fields
    float bikeLean{};               // bike/bmx
    float controlPedaling{};        // bmx
    float planeGearState{};         // plane
    uint16_t miscComponentAngle{};  // automobile/mtruck/plane

    bool onRails = false;  // STATUS_SIMPLE: CCarCtrl::UpdateCarOnRails moves it at a steady speed, no physics
    SVehicleExtras extras{};
    server_time_t sendTime = 0;  // g_serverTime of the sender: the receiver moves the target on by the delay
    bool hasRotorSpeed = false;  // helis, planes, boats: the clone's own ai doesn't spin them
    float rotorSpeed = 0.0f;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
#pragma region IDs
        serialize_pedid(stream, pedid);
        serialize_vehicleid(stream, vehicleid);
        serialize_int(stream, (int&)vehicleSubType, VEHICLE_AUTOMOBILE, VEHICLE_TRAILER);
#pragma endregion
#pragma region clamp stuff
        if (Stream::IsWriting)
        {
            turnSpeed.x = std::clamp(turnSpeed.x, -0.5f, 0.5f);
            turnSpeed.y = std::clamp(turnSpeed.y, -0.5f, 0.5f);
            turnSpeed.z = std::clamp(turnSpeed.z, -0.5f, 0.5f);
            health = std::clamp(health, 0.0f, MAX_VEHICLE_HEALTH);
            gasPedal = std::clamp(gasPedal, -1.0f, 1.0f);
            breakPedal = std::clamp(breakPedal, -1.0f, 1.0f);
            steerAngle = std::clamp(steerAngle, -1.0f, 1.0f);
            bikeLean = std::clamp(bikeLean, -1.0f, 1.0f);
            controlPedaling = std::clamp(controlPedaling, -5.0f, 5.0f);
        }
#pragma endregion
#pragma region matrix speeds
        serialize_object(stream, pos);
        serialize_object(stream, rot);
        serialize_object(stream, roll);
        serialize_object(stream, velocity);

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

#pragma endregion
#pragma region painjob
        serialize_uint8(stream, color1);
        serialize_uint8(stream, color2);
        if (Stream::IsWriting)
        {
            if (paintjob < -1 || paintjob > 2)  // got limits here https://wiki.multitheftauto.com/wiki/Paintjob
            {
                paintjob = -1;
            }
        }
        serialize_int(stream, paintjob, -1, 2);

#pragma endregion
#pragma region ped health
        {
            serialize_object(stream, pedHealth);
            serialize_object(stream, pedWeapon);
        }
#pragma endregion
#pragma region health doors
        serialize_compressed_float(stream, health, 0.0f, MAX_VEHICLE_HEALTH, 1.0f);

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
#pragma endregion
#pragma region pedals
        {
            serialize_compressed_float(stream, gasPedal, -1.0f, 1.0f, 0.01f);
            serialize_compressed_float(stream, breakPedal, -1.0f, 1.0f, 0.01f);
            serialize_compressed_float(stream, steerAngle, -1.0f, 1.0f, 0.01f);
        }
#pragma endregion
#pragma region siren horn
        serialize_bool(stream, siren);
        serialize_uint8(stream, hornCounter);
#pragma endregion
#pragma region type specific

        if (vehicleSubType == VEHICLE_BIKE || vehicleSubType == VEHICLE_BMX)
        {
            serialize_compressed_float(stream, bikeLean, -1.0f, 1.0f, 0.01f);
        }
        if (vehicleSubType == VEHICLE_BMX)
        {
            serialize_compressed_float(stream, controlPedaling, -5.0f, 5.0f, 0.01f);
        }
        if (vehicleSubType == VEHICLE_PLANE)
        {
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
        }
        if (vehicleSubType == VEHICLE_AUTOMOBILE || vehicleSubType == VEHICLE_MTRUCK || vehicleSubType == VEHICLE_PLANE)
        {
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
        }
#pragma endregion

        serialize_bool(stream, onRails);
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

// the syncer's ped started to get into a vehicle (CTaskComplexEnterCarAs*): its clones play the same task, the
// walk to the door and a jacking included, instead of a warp in at the first driver / passenger update
class PedVehicleEnter : public Packet
{
    DEFINE_PACKET_TYPE(PedVehicleEnter, ePacketType::PED_VEHICLE_ENTER, ePacketChannel::EVENT);

public:
    SenderPlayerId senderPlayerid{};  // the syncer, set by the server
    pedid_t pedid{};
    vehicleid_t vehicleid{};
    bool passenger = false;
    int8_t seat = 0;  // CTaskComplexEnterCar::m_nTargetSeat of a passenger

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_pedid(stream, pedid);
        serialize_vehicleid(stream, vehicleid);
        serialize_bool(stream, passenger);
        if (passenger)
        {
            serialize_int(stream, seat, 0, 7);
        }
        return true;
    }
};

// the syncer's ped got out of a vehicle (or started its leave task): the same as VEHICLE_EXIT of a player, the clone
// plays the same way out instead of a warp at the first on foot update (CVehicleExitSync)
class PedVehicleExit : public Packet
{
    DEFINE_PACKET_TYPE(PedVehicleExit, ePacketType::PED_VEHICLE_EXIT, ePacketChannel::EVENT);

public:
    pedid_t pedid{};
    vehicleid_t vehicleid{};
    uint8_t reason = Packets::Vehicles::VEHICLE_EXIT_LEAVE;  // eVehicleExitReason

    // out: where the ped is the frame the vehicle let it go
    WorldPositionCompressed pos{};
    MoveSpeedCompressed moveSpeed{};
    RadianAngleCompressed heading{};
    uint8_t fall = Packets::Vehicles::VEHICLE_EXIT_FALL_NONE;  // eVehicleExitFall
    uint8_t animGroup = 0;
    uint16_t animId = 0;
    uint16_t downTime = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_vehicleid(stream, vehicleid);
        serialize_int(stream, reason, 0, Packets::Vehicles::VEHICLE_EXIT_REASON_COUNT - 1);
        if (reason == Packets::Vehicles::VEHICLE_EXIT_LEAVE)
        {
            return true;
        }

        serialize_object(stream, pos);
        serialize_object(stream, moveSpeed);
        serialize_object(stream, heading);
        serialize_int(stream, fall, 0, Packets::Vehicles::VEHICLE_EXIT_FALL_COUNT - 1);
        if (fall == Packets::Vehicles::VEHICLE_EXIT_FALL_ANIM)
        {
            serialize_uint8(stream, animGroup);
            serialize_uint16(stream, animId);
            serialize_uint16(stream, downTime);
        }
        return true;
    }
};

class PedPassengerSync : public Packet
{
    DEFINE_PACKET_TYPE(PedPassengerSync, ePacketType::PED_PASSENGER_UPDATE, ePacketChannel::SYNC);

public:
    pedid_t pedid{};
    vehicleid_t vehicleid{};

    Packets::Players::SHealthSnapshot healthSnapshot{};
    Packets::Players::SWeaponSnapshot weaponSnapshot{};

    int8_t seatid;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_vehicleid(stream, vehicleid);
        serialize_object(stream, healthSnapshot);
        serialize_object(stream, weaponSnapshot);
        serialize_int(stream, seatid, -1, 7);  // TODO test properly TODO(v0.3.1-alpha): limits
        return true;
    }
};

class PedShotSync : public Packet
{
    DEFINE_PACKET_TYPE(PedShotSync, ePacketType::PED_SHOT_SYNC, ePacketChannel::EVENT);

public:
    pedid_t pedid{};
    eWeaponType weaponType = WEAPON_UNARMED;
    WorldPositionCompressed origin{};
    WorldPositionCompressed effect{};
    WorldPositionCompressed target{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_int(stream, (int&)weaponType, WEAPON_UNARMED, WEAPON_FLARE);
        serialize_object(stream, origin);
        serialize_object(stream, effect);
        serialize_object(stream, target);
        return true;
    }
};

class PedSay : public Packet
{
    DEFINE_PACKET_TYPE(PedSay, ePacketType::PED_SAY, ePacketChannel::EVENT);

public:
    CNetworkEntitySerializer entity{};
    eGlobalSpeechContexts phraseId = CONTEXT_GLOBAL_NO_SPEECH;
    uint32_t startTimeDelay = 0;
    bool overrideSilence = false;
    bool isForceAudible = false;
    bool isFrontEnd = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, entity);
        if (entity.entityType != NETWORK_ENTITY_TYPE_PLAYER && entity.entityType != NETWORK_ENTITY_TYPE_PED)
        {
            return false;
        }
        serialize_int(stream, (int&)phraseId, CONTEXT_GLOBAL_NO_SPEECH + 1, CONTEXT_GLOBAL_END - 1);
        serialize_uint32(stream, startTimeDelay);
        serialize_bool(stream, overrideSilence);
        serialize_bool(stream, isForceAudible);
        serialize_bool(stream, isFrontEnd);
        return true;
    }
};

// a ped (an npc or a player) of the sender died: its clones play the same CTaskComplexDie, the death anim the
// owner's damage response chose (CPedDeathSync); they take no damage of their own (DamageHooks)
class PedDied : public Packet
{
    DEFINE_PACKET_TYPE(PedDied, ePacketType::PED_DIED, ePacketChannel::EVENT);

public:
    static constexpr uint8_t FLAG_STEALTH = 1;             // killed by a stealth kill (CTaskSimpleStealthKill)
    static constexpr uint8_t FLAG_FALLING_TO_DEATH = 2;    // CTaskComplexDie::m_bFallingToDeath
    static constexpr uint8_t FLAG_OVER_RAILING = 4;        // CTaskComplexDie::m_bFallToDeathOverRailing
    static constexpr uint8_t FLAG_DROWNED = 8;

    CNetworkEntitySerializer entity{};
    uint8_t weapon = 0;
    uint8_t animGroup = 0;
    uint16_t animId = 0;
    float blendDelta = 4.0f;
    float animSpeed = 0.0f;
    uint8_t flags = 0;
    uint8_t fallDirection = 0;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, entity);
        if (entity.entityType != NETWORK_ENTITY_TYPE_PLAYER && entity.entityType != NETWORK_ENTITY_TYPE_PED)
        {
            return false;
        }
        serialize_uint8(stream, weapon);
        serialize_uint8(stream, animGroup);
        serialize_uint16(stream, animId);
        serialize_float(stream, blendDelta);
        serialize_float(stream, animSpeed);
        serialize_uint8(stream, flags);
        serialize_uint8(stream, fallDirection);
        return true;
    }
};

// the sender's player started a stealth kill: the victim's owner plays the victim's part (the engine kills it by
// it), the others play both parts; the killer's own damage event can't reach a victim it doesn't own
class PedStealthKill : public Packet
{
    DEFINE_PACKET_TYPE(PedStealthKill, ePacketType::PED_STEALTH_KILL, ePacketChannel::EVENT);

public:
    SenderPlayerId senderPlayerid{};  // the killer
    CNetworkEntitySerializer victim{};
    uint8_t animGroup = 0;  // the knife's anim group

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, senderPlayerid);
        serialize_object(stream, victim);
        if (victim.entityType != NETWORK_ENTITY_TYPE_PLAYER && victim.entityType != NETWORK_ENTITY_TYPE_PED)
        {
            return false;
        }
        serialize_uint8(stream, animGroup);
        return true;
    }
};

class PedClaimOnRelease : public Packet
{
    DEFINE_PACKET_TYPE(PedClaimOnRelease, ePacketType::PED_CLAIM_ON_RELEASE, ePacketChannel::EVENT);

public:
    pedid_t pedid{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        return true;
    }
};

class PedCancelClaim : public Packet
{
    DEFINE_PACKET_TYPE(PedCancelClaim, ePacketType::PED_CANCEL_CLAIM, ePacketChannel::EVENT);

public:
    pedid_t pedid{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        return true;
    }
};

class PedResetAllClaims : public Packet
{
    DEFINE_PACKET_TYPE(PedResetAllClaims, ePacketType::PED_RESET_ALL_CLAIMS, ePacketChannel::EVENT);

public:
    pedid_t pedid{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        return true;
    }
};

class PedTakeHost : public Packet
{
    DEFINE_PACKET_TYPE(PedTakeHost, ePacketType::PED_TAKE_HOST, ePacketChannel::EVENT);

public:
    pedid_t pedid{};
    bool allowReturnToPreviousHost = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_pedid(stream, pedid);
        serialize_bool(stream, allowReturnToPreviousHost);
        return true;
    }
};

}  // namespace Packets::Peds
