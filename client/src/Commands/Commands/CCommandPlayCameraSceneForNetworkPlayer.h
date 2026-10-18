#pragma once
#include "../CCustomCommand.h"
class CCommandPlayCameraSceneForNetworkPlayer : public CCustomCommand
{
	// Inherited via CCustomCommand
	void Process(CRunningScript* script) override;
};
