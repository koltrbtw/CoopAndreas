#include "stdafx.h"
#include "CNetworkCameraScene.h"

CNetworkCameraScene::eState CNetworkCameraScene::ms_eState = eState::NONE;
unsigned int CNetworkCameraScene::ms_nStartTime = 0;
unsigned int CNetworkCameraScene::ms_nDelay = 0;
unsigned int CNetworkCameraScene::ms_nDuration = 0;
CVector CNetworkCameraScene::ms_vecCamPos{};
CVector CNetworkCameraScene::ms_vecLookAt{};
char CNetworkCameraScene::ms_szText[8]{};
char CNetworkCameraScene::ms_szHelp[8]{};

void CNetworkCameraScene::Start(const CVector& camPos, const CVector& lookAt, const char* szText, const char* szHelp,
    unsigned int nDelay, unsigned int nDuration)
{
    ms_vecCamPos = camPos;
    ms_vecLookAt = lookAt;
    strncpy_s(ms_szText, szText, 7);
    strncpy_s(ms_szHelp, szHelp, 7);
    ms_nDelay = nDelay;
    ms_nDuration = nDuration;
    ms_nStartTime = CTimer::m_snTimeInMilliseconds;
    ms_eState = eState::DELAY;

    plugin::Command<Commands::SET_PLAYER_CONTROL>(0, false);
    plugin::Command<Commands::SWITCH_WIDESCREEN>(true);
    plugin::Command<Commands::CLEAR_PRINTS>();
    logger::info("camera scene '%s' of the host's mission", ms_szText);
}

void CNetworkCameraScene::Finish()
{
    plugin::Command<Commands::RESTORE_CAMERA_JUMPCUT>();
    plugin::Command<Commands::SWITCH_WIDESCREEN>(false);
    plugin::Command<Commands::SET_PLAYER_CONTROL>(0, true);
    ms_eState = eState::NONE;
}

void CNetworkCameraScene::Process()
{
    if (ms_eState == eState::NONE)
    {
        return;
    }

    // the mission is over (passed, failed) in the middle of it: the control back at once
    if (!CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag])
    {
        Finish();
        return;
    }

    unsigned int nElapsed = CTimer::m_snTimeInMilliseconds - ms_nStartTime;
    if (ms_eState == eState::DELAY && nElapsed >= ms_nDelay)
    {
        plugin::Command<Commands::SET_FIXED_CAMERA_POSITION>(ms_vecCamPos.x, ms_vecCamPos.y, ms_vecCamPos.z, 0.0f, 0.0f,
            0.0f);
        plugin::Command<Commands::POINT_CAMERA_AT_POINT>(ms_vecLookAt.x, ms_vecLookAt.y, ms_vecLookAt.z, 2);  // jump cut
        if (ms_szText[0])
        {
            plugin::Command<Commands::PRINT_NOW>(ms_szText, ms_nDuration + 2000, 1);
        }
        ms_eState = eState::SHOW;
    }
    else if (ms_eState == eState::SHOW && nElapsed >= ms_nDelay + ms_nDuration)
    {
        Finish();
        if (ms_szHelp[0])
        {
            plugin::Command<Commands::PRINT_HELP>(ms_szHelp);
        }
    }
}
