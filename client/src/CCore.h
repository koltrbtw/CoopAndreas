#pragma once
#include <semver.h>

class CCore
{
public:
	static void Init();
	static void AllocateConsole();

	static inline bool ms_bDebug = false;
	static constexpr int MAX_DEBUG_INSTANCES = 8;

	static inline uint8_t ms_nRunIndex;
	static inline uint8_t ms_nRunSlots = 2;

	static semver_t Version;
	static inline injector::game_version_manager gvm{};
};

