#include "network/packet_types.h"
#include "stdafx.h"
#include "network/packet_handler.h"
#include "network/packets/pickups.h"
#include "CPickupStates.h"

PACKET_HANDLER(ePacketType::PICKUP_COLLECT_REQUEST, Packets::Pickups::PickupCollectRequest* pRequest,
    CNetworkPlayer* pNetworkPlayer)
{
    CPickupStates::HandleRequest(*pRequest, pNetworkPlayer);
}

PACKET_HANDLER(
    ePacketType::PICKUP_COLLECTED, Packets::Pickups::PickupCollected* pCollected, CNetworkPlayer* pNetworkPlayer)
{
    CPickupStates::HandleCollected(*pCollected, pNetworkPlayer);
}
