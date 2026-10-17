#pragma once
class CNetworkVehicle
{
	friend class CTrainSync;

private:
	CNetworkVehicle() {}
public:
	vehicleid_t m_vehicleId{};
	CVehicle* m_pVehicle = nullptr;
	int m_nModelId = 0;
	char m_nPaintJob = -1;
	bool m_bSyncing = false;
	unsigned char m_nCreatedBy;
	uint8_t m_autoPilotStyle = 0;        // from the last handoff
	uint8_t m_autoPilotCruiseSpeed = 0;  // from the last handoff, 0: default
	int m_nBlipHandle = -1;
	unsigned int m_nOwnershipChangeTime = 0;  // when it became ours or someone else's, CTimer time
	Packets::Vehicles::VehicleDriverUpdate m_playerDriverSnapshot{};
	CDamageManager m_oldDamageState{};
	uint8_t m_aOldBikeWheelStatus[2]{};
	// a clone moves towards the syncer's transform by itself (CVehicle physics with the synced speed), each update
	// adds a part of the error over the time to the next one, see SetTargetTransform
	bool m_bHasTarget = false;
	CVector m_vecTargetPosError{};
	CVector m_vecTargetRight{};
	CVector m_vecTargetForward{};
	float m_fTargetRotFactor = 0.0f;
	unsigned int m_nTargetStart = 0;
	unsigned int m_nTargetFinish = 0;
	float m_fTargetLastAlpha = 0.0f;
	unsigned int m_nLastTargetTime = 0;
	int m_nUnderFloorCount = 0;
	// the syncer's npc car goes on rails (no physics, gas 0): the clone's physics would brake it by itself, it keeps
	// the update's speed instead, a while after the update
	bool m_bKeepSpeed = false;
	CVector m_vecKeptMoveSpeed{};
	CVector m_vecKeptTurnSpeed{};
	bool m_bHasRotorSpeed = false;  // a clone: the syncer's rotor / propeller speed, kept every frame
	float m_fRotorSpeed = 0.0f;
	unsigned int m_nWakeTime = 0;  // ours: last push of a car frozen above the ground, CTimer time
	bool m_bDamageApplied = false;  // the first VEHICLE_DAMAGE puts the parts without effects
	// the front carriage of a train, see CTrainSync; the other carriages have no network id
	bool m_bTrain = false;
	TrainState m_trainState{};  // ours: what we last sent, a clone: what the syncer last sent

	~CNetworkVehicle();
	CNetworkVehicle(vehicleid_t vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2, unsigned char createdBy);
	bool CreateVehicle(vehicleid_t vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2);
	bool HasDriver();

	// pHandoff: the vehicle was a clone of someone else's ambient vehicle, continue its life from this state
	void BecomeSyncer(const Packets::Vehicles::VehicleHandoff* pHandoff);
	// gives our npc driver the ambient driving task, once both the vehicle and the driver are ours
	void ResumeDriving();

	// tow links: the network id of what this vehicle tows, and attaching/detaching our clone of it to match
	static vehicleid_t GetTrailerId(CVehicle* pVehicle);
	void ApplyTrailer(vehicleid_t trailerid);
	// fills pHandoff with what the new syncer needs and turns the vehicle and our npc peds inside into clones
	void BecomeClone(Packets::Vehicles::VehicleHandoff* pHandoff);

	// a clone: the transform from an update, the speeds go to the physics straight away
	void SetTargetTransform(const CVector& pos, const CVector& right, const CVector& forward, const CVector& moveSpeed,
		const CVector& turnSpeed);
	// a clone, every frame after its ProcessControl
	void ProcessTargetTransform();
	// rotor of a heli, propeller of a plane or a boat; false for the others
	static bool GetRotorSpeed(CVehicle* pVehicle, float* pfSpeed);
	static void SetRotorSpeed(CVehicle* pVehicle, float fSpeed);

	// ours: the damage changed since the last VEHICLE_DAMAGE, fills pPacket
	bool CollectDamage(Packets::Vehicles::VehicleDamage* pPacket);
	// a clone: the damage state of the syncer, repairs included
	void ApplyDamage(const Packets::Vehicles::VehicleDamage& damage);
	
	static CNetworkVehicle* CreateHosted(CVehicle* vehicle);

	// before a clone is deleted: CVehicle::~CVehicle destroys the peds inside, a remote player's one too
	static void TakeOutNetworkPlayers(CVehicle* pVehicle);
};

