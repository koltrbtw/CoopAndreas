#pragma once
class CPlayerPed;

class CPassengerEnter
{
public:
	static void Process();
	// G: into the passenger door of the nearest network vehicle, false when there is none free
	static bool EnterNearestVehicle(CPlayerPed* pPlayerPed);
};
