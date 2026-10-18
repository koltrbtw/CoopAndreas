#pragma once
#include "../CCustomCommand.h"
class CCommandPauseSyncingThisScript : public CCustomCommand
{
	// Inherited via CCustomCommand
	void Process(CRunningScript* script) override;
};
