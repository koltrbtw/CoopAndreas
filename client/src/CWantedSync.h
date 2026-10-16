#pragma once

#include "stdafx.h"

// wanted level is shared inside a group of players who are close to each other (a gang): whoever gets any wanted level
// there is raised to the group maximum. Players far away keep their own level, everyone loses it on their own
class CWantedSync
{
public:
    static constexpr float GROUP_RADIUS = 150.0f;

    static void Reset();
    static void Process();
    static void HandlePacket(Packets::Players::PlayerWantedLevel* pPlayerWantedLevel);

private:
    static unsigned int GetGroupMaxWantedLevel(const CVector& localPosition);

    static inline int ms_nLastSentWantedLevel = -1;
};
