#pragma once
#include "../CCustomCommand.h"
class CCommandCreateObjectForNetworkPlayer : public CCustomCommand
{
	// Inherited via CCustomCommand
	void Process(CRunningScript* script) override;
};
