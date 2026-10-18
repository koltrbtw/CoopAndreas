#include "network/packets/pickups.h"
#include "network/packet_types.h"
#include "stdafx.h"

PACKET_HANDLER(ePacketType::PICKUP_GRANT, Packets::Pickups::PickupGrant* pGrant)
{
    CPickupSync::HandleGrant(*pGrant);
}

PACKET_HANDLER(ePacketType::PICKUP_STATE, Packets::Pickups::PickupState* pState)
{
    CPickupSync::HandleState(*pState);
}
