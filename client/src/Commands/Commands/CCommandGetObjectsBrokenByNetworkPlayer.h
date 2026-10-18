#pragma once
#include "../CCustomCommand.h"
class CCommandGetObjectsBrokenByNetworkPlayer : public CCustomCommand
{
	// Inherited via CCustomCommand
	void Process(CRunningScript* script) override;
};
