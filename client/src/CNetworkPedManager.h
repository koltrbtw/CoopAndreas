#pragma once
class CNetworkPedManager
{
public:
	static std::vector<CNetworkPed*> m_pPeds;

	static inline uint32_t ms_nNextPedId = 1;

	static CNetworkPed* GetPed(pedid_t pedid);
	static CNetworkPed* GetPed(CEntity* entity);
	static void Add(CNetworkPed* ped);
	static void Remove(CNetworkPed* ped);
	static void Update();
	static void Process();
	static pedid_t AllocatePedId();
	static void ResetIdAllocator();
	static void RemoveHostedUnused();

	// a clone whose model is not in memory yet waits for it instead of a synchronous load (a frame freeze, and many
	// peds come at once when a player gets in scope)
	static void HandleSpawn(const Packets::Peds::PedSpawn& spawn);
	static void CancelPendingSpawn(pedid_t pedid);
	static bool DeferHandoff(const Packets::Peds::PedHandoff& handoff);
	static void ProcessPendingSpawns();

private:
	struct SPendingSpawn
	{
		Packets::Peds::PedSpawn spawn{};
		bool bHasHandoff = false;
		Packets::Peds::PedHandoff handoff{};
		unsigned int nSince = 0;
	};
	static inline std::vector<SPendingSpawn> ms_pendingSpawns;

	static void CreateClone(const Packets::Peds::PedSpawn& spawn);
};

