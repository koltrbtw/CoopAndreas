#include "stdafx.h"
#include "CScreenLog.h"
#include <CHud.h>
#include <CMessages.h>
#include <CUserDisplay.h>
#include <CCutsceneMgr.h>
#include <CRadar.h>
#include "CNetworkCheckpoint.h"

namespace
{
constexpr size_t MAX_TEXT = 160;
constexpr int NUM_BIG_MESSAGES = 7;
constexpr int NUM_COUNTERS = 4;
constexpr int NUM_RADAR_TRACES = 175;

std::string Text(const char* szText)
{
    if (!szText)
    {
        return {};
    }
    std::string text(szText, strnlen(szText, MAX_TEXT));
    for (char& c : text)
    {
        if (static_cast<unsigned char>(c) < 0x20)
        {
            c = ' ';
        }
    }
    return text;
}

std::string s_subtitle;
std::string s_help;
std::string s_big[NUM_BIG_MESSAGES];
bool s_bWidescreen = false;
bool s_bControl = true;
bool s_bFixedCamera = false;
CVector s_vecCamera{};
bool s_bCutscene = false;
bool s_counters[NUM_COUNTERS]{};

const char* BlipColour(unsigned int nColour)
{
    static const char* names[] = {"red", "green", "blue", "white", "yellow", "purple", "cyan", "threat", "destination"};
    return nColour < std::size(names) ? names[nColour] : "rgba";
}
}  // namespace

void CScreenLog::Process()
{
    if (!CCore::ms_bDebug)
    {
        return;
    }

    std::string subtitle = Text(CMessages::BriefMessages[0].m_pText);
    if (subtitle != s_subtitle)
    {
        s_subtitle = subtitle;
        if (subtitle.empty())
        {
            logger::info("screen: subtitle cleared");
        }
        else
        {
            logger::info("screen: subtitle '%s'", subtitle.c_str());
        }
    }

    std::string help = Text(CHud::m_pHelpMessage);
    if (help != s_help)
    {
        s_help = help;
        if (help.empty())
        {
            logger::info("screen: help cleared");
        }
        else
        {
            logger::info("screen: help '%s'", help.c_str());
        }
    }

    for (int i = 0; i < NUM_BIG_MESSAGES; i++)
    {
        std::string big = Text(CHud::m_BigMessage[i]);
        if (big != s_big[i])
        {
            s_big[i] = big;
            if (!big.empty())
            {
                logger::info("screen: big %d '%s'", i, big.c_str());
            }
        }
    }

    if (TheCamera.m_bWideScreenOn != s_bWidescreen)
    {
        s_bWidescreen = TheCamera.m_bWideScreenOn;
        logger::info("screen: widescreen %s", s_bWidescreen ? "on" : "off");
    }

    CPad* pPad = CPad::GetPad(0);
    bool bControl = pPad && pPad->DisablePlayerControls == 0;
    if (bControl != s_bControl)
    {
        s_bControl = bControl;
        logger::info("screen: control %s", bControl ? "on" : "off");
    }

    const CCam& cam = TheCamera.m_aCams[TheCamera.m_nActiveCam];
    bool bFixed = cam.m_nMode == MODE_FIXED;
    if (bFixed != s_bFixedCamera || (bFixed && (cam.m_vecSource - s_vecCamera).Magnitude() > 0.5f))
    {
        s_bFixedCamera = bFixed;
        s_vecCamera = cam.m_vecSource;
        if (bFixed)
        {
            logger::info("screen: camera fixed at %.2f %.2f %.2f, front %.2f %.2f %.2f", cam.m_vecSource.x,
                cam.m_vecSource.y, cam.m_vecSource.z, cam.m_vecFront.x, cam.m_vecFront.y, cam.m_vecFront.z);
        }
        else
        {
            logger::info("screen: camera mode %d", static_cast<int>(cam.m_nMode));
        }
    }

    if (CCutsceneMgr::ms_running != s_bCutscene)
    {
        s_bCutscene = CCutsceneMgr::ms_running;
        logger::info("screen: cutscene %s", s_bCutscene ? "running" : "over");
    }

    for (int i = 0; i < NUM_COUNTERS; i++)
    {
        const auto& counter = CUserDisplay::OnscnTimer.m_aCounters[i];
        if (counter.m_bEnabled != s_counters[i])
        {
            s_counters[i] = counter.m_bEnabled;
            logger::info("screen: counter %d '%s' %s", i, Text(counter.m_szDescriptionTextKey).c_str(),
                counter.m_bEnabled ? "on" : "off");
        }
    }
}

void CScreenLog::DumpState()
{
    logger::info("hud: subtitle '%s'", s_subtitle.c_str());
    logger::info("hud: help '%s'", s_help.c_str());
    for (int i = 0; i < NUM_BIG_MESSAGES; i++)
    {
        if (!s_big[i].empty())
        {
            logger::info("hud: big %d '%s'", i, s_big[i].c_str());
        }
    }

    const CCam& cam = TheCamera.m_aCams[TheCamera.m_nActiveCam];
    logger::info("hud: widescreen %d, control %d, cutscene %d, camera mode %d at %.2f %.2f %.2f", s_bWidescreen,
        s_bControl, s_bCutscene, static_cast<int>(cam.m_nMode), cam.m_vecSource.x, cam.m_vecSource.y,
        cam.m_vecSource.z);

    for (int i = 0; i < NUM_COUNTERS; i++)
    {
        const auto& counter = CUserDisplay::OnscnTimer.m_aCounters[i];
        if (counter.m_bEnabled)
        {
            logger::info("hud: counter %d '%s' value %d", i, Text(counter.m_szDescriptionTextKey).c_str(),
                *reinterpret_cast<int*>(&CTheScripts::ScriptSpace[counter.m_nVarId]));
        }
    }

    if (CNetworkCheckpoint::ms_bEnabled)
    {
        logger::info("hud: checkpoint (coop) at %.1f %.1f %.1f", CNetworkCheckpoint::ms_vecPosition.x,
            CNetworkCheckpoint::ms_vecPosition.y, CNetworkCheckpoint::ms_vecPosition.z);
    }

    // what this player is pointed to; the map icons (shops, safehouses: a coord with a sprite) are left out
    for (int i = 0; i < NUM_RADAR_TRACES; i++)
    {
        const tRadarTrace& trace = CRadar::ms_RadarTrace[i];
        if (!trace.m_bInUse || trace.m_nRadarSprite != 0)
        {
            continue;
        }

        std::string what;
        switch (trace.m_nBlipType)
        {
            case BLIP_CAR:
                if (CVehicle* pVehicle = CPools::GetVehicle(trace.m_nEntityHandle))
                {
                    what = "car model " + std::to_string(pVehicle->m_nModelIndex);
                }
                break;
            case BLIP_CHAR:
                if (CPed* pPed = CPools::GetPed(trace.m_nEntityHandle))
                {
                    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(pPed);
                    what = pNetworkPlayer ? "char player " + pNetworkPlayer->GetName()
                                          : "char model " + std::to_string(pPed->m_nModelIndex);
                }
                break;
            case BLIP_COORD:
            case BLIP_CONTACTPOINT:
            {
                char pos[64];
                sprintf_s(pos, "coord %.0f %.0f %.0f", trace.m_vecPos.x, trace.m_vecPos.y, trace.m_vecPos.z);
                what = pos;
                break;
            }
            default:
                what = "type " + std::to_string(trace.m_nBlipType);
                break;
        }
        logger::info("hud: blip %d %s %s%s", i, what.c_str(), BlipColour(trace.m_nColour),
            trace.m_bFriendly ? " friendly" : "");
    }
}
