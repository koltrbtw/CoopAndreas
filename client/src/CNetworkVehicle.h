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
	
	static CNetworkVehicle* CreateHosted(CVehicle* vehicle);

	// before a clone is deleted: CVehicle::~CVehicle destroys the peds inside, a remote player's one too
	static void TakeOutNetworkPlayers(CVehicle* pVehicle);
};

