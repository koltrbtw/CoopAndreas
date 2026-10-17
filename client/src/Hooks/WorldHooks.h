#pragma once
class WorldHooks
{
public: 
	static void InjectHooks();
	// CWorld::Remove and the network side of it (remove packet of an entity we sync)
	static void __cdecl CWorld__Remove_Hook(CEntity* entity);

	static std::unordered_map<CVehicle*, std::vector<void*>> m_aVehicleStackMap;
};

