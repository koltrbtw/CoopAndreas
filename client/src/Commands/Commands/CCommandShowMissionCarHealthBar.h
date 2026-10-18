#pragma once
#include "../CCustomCommand.h"
class CCommandShowMissionCarHealthBar : public CCustomCommand
{
	// Inherited via CCustomCommand
	void Process(CRunningScript* script) override;
};
