#pragma once
class CNetworkPed
{
private:
	CNetworkPed() {}
public:
	pedid_t m_pedId{};
	CPed* m_pPed = nullptr;
	bool m_bSyncing = false;
	ePedType m_nPedType;
	unsigned char m_nCreatedBy;
	CVector m_vecVelocity{0.0f, 0.0f, 0.0f};
	float m_fAimingRotation = 0.0f;
	float m_fCurrentRotation = 0.0f;
	float m_fLookDirection = 0.0f;
	eMoveState m_nMoveState = eMoveState::PEDMOVE_NONE;
	float m_fMoveBlendRatio = 0.0f;
	CAutoPilot m_autoPilot;
	float m_fGasPedal = 0.0f;
	float m_fBreakPedal = 0.0f;
	float m_fSteerAngle = 0.0f;
	float m_fHealth = 100.0f;
	int m_nBlipHandle = -1;
	bool m_bClaimOnRelease = false;
	unsigned int m_nOwnershipChangeTime = 0;  // when it became ours or someone else's, CTimer time
	// a mission ped we sync for the host while it has no collision there, see CPopulationHandover::Process
	bool m_bBorrowed = false;
	CVector m_vecTargetPos{};  // the last on foot position from the syncer
	unsigned int m_nTargetPosTime = 0;
	bool m_bHasTargetPos = false;
	// a clone: the vehicle of the last PED_VEHICLE_ENTER and CTimer time it came, our copy of the enter task runs
	// out of step with the syncer's (later, its own path to the door)
	vehicleid_t m_enterVehicleId{};
	unsigned int m_nEnterVehicleTime = 0;
	// ours: the vehicle PED_VEHICLE_ENTER went for, while the enter task lasts
	CVehicle* m_pEnterSentVehicle = nullptr;

	static CNetworkPed* CreateHosted(CPed* ped);
	void WarpIntoVehicleDriver(CVehicle* vehicle);
	void WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid);
	void RemoveFromVehicle(CVehicle* vehicle);
	// PED_VEHICLE_ENTER: our copy of the syncer's enter task
	void EnterVehicle(CVehicle* vehicle, bool bPassenger, int seat, playerid_t syncer);
	// still on the way in by our copy of the enter task
	bool IsEnteringVehicle(CVehicle* vehicle);
	// sat in the vehicle of the last PED_VEHICLE_ENTER: the syncer's last on foot updates may still come
	bool HasJustEnteredVehicle();
	// ours: PED_VEHICLE_ENTER when its enter task starts
	void CollectVehicleEnter();
	// its enter task runs (a jacking too)
	bool IsGettingIntoVehicle();
	void ClaimOnRelease();
	void CancelClaim();

	// bResumeAmbientLife: the ped was a clone of someone else's ambient ped, give it back its own ai
	void BecomeSyncer(bool bResumeAmbientLife);
	// bStopAmbientLife: our ambient ped goes to someone else, stop its own ai
	void BecomeClone(bool bStopAmbientLife);
	// PED_HANDOFF: the ped is ours from now on
	void ApplyHandoff(const Packets::Peds::PedHandoff& handoff);

	void ApplyWeaponSnapshot(Packets::Players::SWeaponSnapshot& weaponSnapshot);
	// on foot clone: the position from a packet, ProcessTargetPosition pulls the ped to it every frame
	void SetTargetPosition(const CVector& pos, const CVector& velocity);
	void ProcessTargetPosition();

	CNetworkPed(pedid_t pedid, int modelId, ePedType pedType, CVector pos, unsigned char createdBy, char specialModelName[]);
	~CNetworkPed();
};

