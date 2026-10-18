#pragma once
#include "../CCustomCommand.h"
class CCommandRemoveMissionCarHealthBar : public CCustomCommand
{
	// Inherited via CCustomCommand
	void Process(CRunningScript* script) override;
};
