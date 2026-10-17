#pragma once

#include <iostream>
#include <vector>
#include <algorithm>

#include "CVector.h"
#include "CNetworkPlayerManager.h"
#include <CDamageManager.h>

class CNetworkVehicle
{
public:
    CNetworkVehicle(vehicleid_t vehicleid, unsigned short model, CVector pos, float rot);

    vehicleid_t m_vehicleId;
    CNetworkPlayer* m_pSyncer = nullptr;
    // asked for the vehicle, waits for the handoff from m_pSyncer, see VEHICLE_REQUEST_OWNERSHIP
    CNetworkPlayer* m_pPendingSyncer = nullptr;
    unsigned short m_nModelId;
    CVector m_vecPosition;
    CVector m_vecRotation;
    CVector m_vecVelocity;
    CNetworkPlayer* m_pPlayers[8] = {nullptr};
    unsigned char m_nPrimaryColor;
    unsigned char m_nSecondaryColor;
    CDamageManager m_damageManager;
    std::vector<int> m_pComponents;
    uint8_t m_nCreatedBy;
    bool m_bUsedByPed = false;
    CVector m_vecRight{1.0f, 0.0f, 0.0f};        // matrix right vector, the updates carry no forward
    std::vector<CNetworkPlayer*> m_vRelevantTo;  // have a clone of it, see CRelevanceManager

    float GetHeading() const;

    void ReassignSyncer(CNetworkPlayer* newSyncer);
    void SetOccupant(int8_t seatid, CNetworkPlayer* player);

    ~CNetworkVehicle() {}
};