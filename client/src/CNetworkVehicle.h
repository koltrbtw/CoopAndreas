#pragma once
class CNetworkVehicle
{
private:
	CNetworkVehicle() {}
public:
	vehicleid_t m_vehicleId{};
	CVehicle* m_pVehicle = nullptr;
	int m_nModelId = 0;
	char m_nPaintJob = -1;
	bool m_bSyncing = false;
	unsigned char m_nCreatedBy;
	int m_nBlipHandle = -1;
	Packets::Vehicles::VehicleDriverUpdate m_playerDriverSnapshot{};
	CDamageManager m_oldDamageState{};

	~CNetworkVehicle();
	CNetworkVehicle(vehicleid_t vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2, unsigned char createdBy);
	bool CreateVehicle(vehicleid_t vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2);
	bool HasDriver();

	// pHandoff: the vehicle was a clone of someone else's ambient vehicle, continue its life from this state
	void BecomeSyncer(const Packets::Vehicles::VehicleHandoff* pHandoff);
	// fills pHandoff with what the new syncer needs and turns the vehicle and our npc peds inside into clones
	void BecomeClone(Packets::Vehicles::VehicleHandoff* pHandoff);
	
	static CNetworkVehicle* CreateHosted(CVehicle* vehicle);
};

