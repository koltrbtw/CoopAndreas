#pragma once
#include "../CCustomCommand.h"
class CCommandRemoveObjectsForNetworkPlayer : public CCustomCommand
{
	// Inherited via CCustomCommand
	void Process(CRunningScript* script) override;
};
