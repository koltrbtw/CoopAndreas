#pragma once

class CNetworkPlayer
{
public:
	CPlayerPed* m_pPed = nullptr;
	playerid_t m_playerId;

	// reusable [0; MAX_SERVER_PLAYERS) index for the game player arrays, unlike `m_playerId`
	int m_nSlot = -1;

	Packets::Players::OnFootUpdate m_onFootSnapshotInterpolated{};
	
	uint32_t m_startedInterpolatingCameraAt = 0;
	Packets::Players::PlayerCameraSync m_cameraSnapshotOld{};
	Packets::Players::PlayerCameraSync m_cameraSnapshot{};
	
	signed short m_oShockButtonL;
	signed short m_lShockButtonL;

	Packets::Players::PlayerPlaceWaypoint m_waypointState{};

	char m_Name[Config::MAX_NICKNAME_LENGTH + 1] = { 0 };
	uint16_t m_nRTT = 0;

	CControllerState m_oldControllerState{};
	CControllerState m_newControllerState{};

	CNetworkPlayerStats m_stats{};
	uint8_t m_nWantedLevel = 0;
	CPedClothesDesc m_pPedClothesDesc{};
	bool m_bHasBeenConnectedBeforeMe = false;

	bool m_bExtrapolating = false;

	bool m_bRequestedDuckTask = false;

	bool m_bIsHost = false;

	// the vehicle of the last VEHICLE_ENTER and CTimer time it came: this game's enter task runs out of step with the
	// sender's, the late on foot / early vehicle updates of the sender must not pull the ped out or warp it in
	vehicleid_t m_enterVehicleId;
	unsigned int m_nEnterVehicleTime = 0;
	// CTimer time of the last VEHICLE_EXIT that started our copy of the leave task, the sender is out before it
	unsigned int m_nLeaveVehicleTime = 0;
	// the vehicle of the last VEHICLE_EXIT (out) and CTimer time it came: a vehicle update the sender sent before it
	// may come later (another channel), it must not put the ped back in
	vehicleid_t m_exitVehicleId;
	unsigned int m_nExitVehicleTime = 0;
	// CTimer time until our copy of a jacker's enter task may drag this ped out (bDontDragMeOutCar off)
	unsigned int m_nJackAllowedUntil = 0;
	// VEHICLE_EXIT (out) that waits for our copy of its leave or of its jacker, CVehicleExitSync::ProcessClones
	std::optional<Packets::Vehicles::VehicleExit> m_pendingExit;

	// on foot: the target CPlayerPositionSync pulls the clone to, in the frame of the vehicle the sender stands on if
	// it does; the epoch of the last update taken (-1: none yet)
	CVector m_vecTargetPos{};
	CVector m_vecTargetVelocity{};
	unsigned int m_nTargetPosTime = 0;
	bool m_bHasTargetPos = false;
	vehicleid_t m_targetContactVehicle{};
	CVector m_vecTargetContactOffset{};
	int m_nEpoch = -1;
	uint8_t m_nLastStateFlags = 0;  // CPedStateSync flags of the last on foot update

	~CNetworkPlayer();
	CNetworkPlayer(playerid_t id, CVector position);

	void CreatePed(CVector position);
	void DestroyPed();
	void Respawn();
	int GetInternalId();
	std::string GetName();
	char GetWeaponSkill(eWeaponType weaponType);
	float GetMaxHealth();
	void WarpIntoVehicleDriver(CVehicle* vehicle);
	// bKnockedOff: thrown out (CTaskSimpleCarSetPedOut::m_bKnockedOffBike), it falls instead of standing up
	void RemoveFromVehicle(CVehicle* vehicle, bool bKnockedOff = false);
	// VEHICLE_EXIT_LEAVE: our copy of the sender's leave task
	void LeaveVehicle(CVehicle* vehicle);
	bool HasJustLeftVehicle(vehicleid_t vehicleid);
	// a ped of this player gets into the vehicle it sits in: the player's game jacks it, our copy may too
	void AllowJack(unsigned int nTime);
	bool IsJackAllowed();
	// still on the way out by our copy of the leave task
	bool IsLeavingVehicle(CVehicle* vehicle);
	void WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid);
	void EnterVehiclePassenger(CVehicle* vehicle, int seatid);
	void OnEnterVehicle(vehicleid_t vehicleid);
	// still on the way in by the enter task of the last VEHICLE_ENTER
	bool IsEnteringVehicle(CVehicle* vehicle);
	// sat in the vehicle of the last VEHICLE_ENTER, no vehicle update of the sender yet: it may still be on the way in
	bool HasJustEnteredVehicle();
	void HandleTask(Packets::Players::SetPlayerTask& packet);
	void ApplyWeaponSnapshot(Packets::Players::SWeaponSnapshot& weaponSnapshot);
};

