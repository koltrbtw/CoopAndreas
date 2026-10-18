#pragma once

// a short scripted camera scene of the host's mission played for one network player when he gets there himself (the
// help scene in the Binco shop): the host's camera opcodes would move everyone's camera at once, wherever they are.
// No control, widescreen, after a delay the fixed camera and the text, then the camera back and the help
class CNetworkCameraScene
{
public:
    static void Start(const CVector& camPos, const CVector& lookAt, const char* szText, const char* szHelp,
        unsigned int nDelay, unsigned int nDuration);
    static void Process();

private:
    enum class eState
    {
        NONE,
        DELAY,
        SHOW,
    };

    static void Finish();

    static eState ms_eState;
    static unsigned int ms_nStartTime;
    static unsigned int ms_nDelay;
    static unsigned int ms_nDuration;
    static CVector ms_vecCamPos;
    static CVector ms_vecLookAt;
    static char ms_szText[8];
    static char ms_szHelp[8];
};
