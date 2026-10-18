#include "stdafx.h"
#include "PlayerHooks.h"
#include "VehicleHooks.h"
#include "TaskHooks.h"
#include "WorldHooks.h"
#include "PedHooks.h"
#include "GameHooks.h"
#include "StatsHooks.h"
#include "CrashfixHooks.h"
#include "MissionAudioHooks.h"
#include "Commands/CommandHooks.h"
#include "SaveLoadHooks.h"
#include "RadarHooks.h"
#include "ProjectileHooks.h"
#include "DamageHooks.h"
#include "ShadowHooks.h"
#include "CCarPedGenHooks.h"
#include "../CPopulationHandover.h"
#include "../CWorldGridSync.h"
#include "../CTrainSync.h"
#include "../CGangZonesSync.h"
#include "../CGangWarSync.h"
#include "../CTrafficLightsSync.h"
#include "../CRadialPopulation.h"
#include "CCustomText.h"

void CHook::Init()
{
    CDriveBy::InitHooks();
    VehicleHooks::InjectHooks();
    PlayerHooks::InjectHooks();
    TaskHooks::InjectHooks();
    CCustomText::InjectHooks();
    WorldHooks::InjectHooks();
    PedHooks::InjectHooks();
    GameHooks::InjectHooks();
    StatsHooks::InjectHooks();
    CommandHooks::InjectHooks();
    CrashfixHooks::InjectHooks();
    MissionAudioHooks::InjectHooks();
    SaveLoadHooks::InjectHooks();
    RadarHooks::InjectHooks();
    ProjectileHooks::InjectHooks();
    DamageHooks::InjectHooks();
    ShadowHooks::InjectHooks();
    CCarPedGenHooks::InjectHooks();
    CPopulationHandover::InjectHooks();  // after WorldHooks, overlaps its CWorld::Remove call sites
    CWorldGridSync::InjectHooks();
    CTrainSync::InjectHooks();
    CTrafficLightsSync::InjectHooks();
    CGangZonesSync::InjectHooks();
    CGangWarSync::InjectHooks();
    CRadialPopulation::InjectHooks();  // after CCarPedGenHooks, overrides some of its call sites
    CVehicleBlowSync::InjectHooks();
    CVehicleCloneDamage::InjectHooks();
    CPickupSync::InjectHooks();
}