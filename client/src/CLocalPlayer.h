#pragma once
class CLocalPlayer
{
public:
	static inline float m_vecLastAimX;
	static inline float m_vecLastAimY;
	static inline char m_Name[Config::MAX_NICKNAME_LENGTH + 1];
	static inline bool m_bIsHost;
	// the host's $onmission (ON_MISSION_FLAG_SYNC): our own main.scm may set it back to 0 (its start, a contact script)
	// while the host's mission runs, it is put back every frame
	static inline bool m_bHostOnMission;

	static void BuildTaskPacket(eTaskType type, bool toggle = true);

	static bool GetIsHostingEntity(CEntity* pEntity);

	// every frame: a drive-by gun given while we sit in a vehicle (a mission's Uzi) taken in hand as getting in does
	static void ProcessVehicleWeapon();
};

