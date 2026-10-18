#pragma once
#include "../CCustomCommand.h"
class CCommandBreakObjectForNetworkPlayer : public CCustomCommand
{
	// Inherited via CCustomCommand
	void Process(CRunningScript* script) override;
};
