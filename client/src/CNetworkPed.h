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
	CVector m_vecTargetPos{};  // the last on foot position from the syncer
	unsigned int m_nTargetPosTime = 0;
	bool m_bHasTargetPos = false;

	static CNetworkPed* CreateHosted(CPed* ped);
	void WarpIntoVehicleDriver(CVehicle* vehicle);
	void WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid);
	void RemoveFromVehicle(CVehicle* vehicle);
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

