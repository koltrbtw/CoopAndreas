#pragma once
#include "../packet.h"

namespace Packets::Pickups
{
// a pickup every game has of its own (INITIAL, MAIN, the IPL ones): the same model, type and compressed position
// (CPickup::m_vecPos, 1/8 m) everywhere, while its slot in CPickups::aPickUps differs
struct PickupKey
{
    int16_t model = 0;
    uint8_t type = 0;
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;

    bool operator==(const PickupKey& other) const
    {
        return model == other.model && type == other.type && x == other.x && y == other.y && z == other.z;
    }

    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, model, -1, 32767);
        serialize_uint8(stream, type);
        serialize_int(stream, x, -32768, 32767);
        serialize_int(stream, y, -32768, 32767);
        serialize_int(stream, z, -32768, 32767);
        return true;
    }
};

struct PickupKeyHash
{
    size_t operator()(const PickupKey& key) const
    {
        size_t hash = static_cast<uint16_t>(key.model);
        hash = hash * 31 + key.type;
        hash = hash * 31 + static_cast<uint16_t>(key.x);
        hash = hash * 31 + static_cast<uint16_t>(key.y);
        hash = hash * 31 + static_cast<uint16_t>(key.z);
        return hash;
    }
};

// we stand on it: may we take it? Repeated while we stand there, the server keeps the lock of the first one
class PickupCollectRequest : public Packet
{
    DEFINE_PACKET_TYPE(PickupCollectRequest, ePacketType::PICKUP_COLLECT_REQUEST, ePacketChannel::EVENT);

public:
    PickupKey key{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, key);
        return true;
    }
};

// server -> the one with the lock: its game may give it (CPickup::Update) for a while
class PickupGrant : public Packet
{
    DEFINE_PACKET_TYPE(PickupGrant, ePacketType::PICKUP_GRANT, ePacketChannel::EVENT);

public:
    PickupKey key{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, key);
        return true;
    }
};

// the game gave it to us: how long it stays away (the regeneration of an on street one), or for good
class PickupCollected : public Packet
{
    DEFINE_PACKET_TYPE(PickupCollected, ePacketType::PICKUP_COLLECTED, ePacketChannel::EVENT);

public:
    static constexpr int32_t FOREVER = -1;

    PickupKey key{};
    int32_t regenerationMs = FOREVER;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, key);
        serialize_int(stream, regenerationMs, -1, 600000);
        return true;
    }
};

// a pickup one game drops (the money and weapons of a body, a jetpack, a gang war's armour): the others make the
// same one, then its key is the same everywhere and it is collected like a map one
class PickupSpawn : public Packet
{
    DEFINE_PACKET_TYPE(PickupSpawn, ePacketType::PICKUP_SPAWN, ePacketChannel::EVENT);

public:
    static constexpr int32_t NO_TIMEOUT = -1;

    PickupKey key{};
    uint32_t ammo = 0;  // the ammo of a weapon, the amount of money
    bool isEmpty = false;
    int32_t remainingMs = NO_TIMEOUT;  // till it goes by itself (CPickup::m_nRegenerationTime of a timeout one)

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, key);
        serialize_uint32(stream, ammo);
        serialize_bool(stream, isEmpty);
        serialize_int(stream, remainingMs, -1, 600000);
        return true;
    }
};

// server -> the others (and a late one): somebody took it, back in remainingMs or never
class PickupState : public Packet
{
    DEFINE_PACKET_TYPE(PickupState, ePacketType::PICKUP_STATE, ePacketChannel::EVENT);

public:
    PickupKey key{};
    int32_t remainingMs = PickupCollected::FOREVER;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, key);
        serialize_int(stream, remainingMs, -1, 600000);
        return true;
    }
};
}
