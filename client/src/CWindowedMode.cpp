#include "stdafx.h"
#include "CWindowedMode.h"

// sizes the game window to half of the primary monitor work area (16:9 client area),
// first instance on the left edge, second one on the right edge
static void PlaceWindow(HWND hWnd, int& clientWidth, int& clientHeight)
{
    const LONG style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE | WS_CLIPSIBLINGS;

    RECT workArea{};
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);

    RECT frame = {0, 0, 0, 0};
    AdjustWindowRect(&frame, style, FALSE);
    const int frameWidth = frame.right - frame.left;
    const int frameHeight = frame.bottom - frame.top;

    const int workWidth = workArea.right - workArea.left;
    const int workHeight = workArea.bottom - workArea.top;

    clientWidth = workWidth / 2 - frameWidth;
    clientHeight = clientWidth * 9 / 16;
    if (clientHeight > workHeight - frameHeight)
    {
        clientHeight = workHeight - frameHeight;
        clientWidth = clientHeight * 16 / 9;
    }

    const int outerWidth = clientWidth + frameWidth;
    const int outerHeight = clientHeight + frameHeight;
    const int x = CCore::ms_nRunIndex == 1 ? workArea.right - outerWidth : workArea.left;

    SetWindowLongW(hWnd, GWL_STYLE, style);
    SetWindowLongW(hWnd, GWL_EXSTYLE, 0);
    SetWindowPos(hWnd, HWND_NOTOPMOST, x, workArea.top, outerWidth, outerHeight, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

// psSelectDevice -> RwEngineSetVideoMode
static RwBool __cdecl RwEngineSetVideoMode_Hook(RwInt32 modeIndex)
{
    // video mode 0 of the D3D9 driver is the windowed (non-exclusive) mode
    RwVideoMode videoMode{};
    RwEngineGetVideoModeInfo(&videoMode, 0);

    if (videoMode.flags & rwVIDEOMODEEXCLUSIVE)
    {
        logger::warn("CWindowedMode - windowed video mode is unavailable, using the selected one");
        return RwEngineSetVideoMode(modeIndex);
    }

    int width = 0, height = 0;
    PlaceWindow(RsGlobal.ps->window, width, height);

    RsGlobal.maximumWidth = width;
    RsGlobal.maximumHeight = height;

    if (!RwEngineSetVideoMode(0))
    {
        return FALSE;
    }

    // current video mode index, the game checks its exclusive flag to decide if it runs fullscreen
    *(int*)0x8D6220 = 0;

    // don't let the menu settings switch back to the saved fullscreen resolution (psSelectScreenVM)
    patch::PutRetn0(0x745C70);

    return TRUE;
}

void CWindowedMode::Init()
{
    // skip the device selection dialog shown on multi-monitor setups, use the saved video mode path
    patch::SetUChar(0x746225, 0xEB);  // jle -> jmp

    patch::RedirectCall(0x7462EA, RwEngineSetVideoMode_Hook);

    // skip the fullscreen-only setup after the video mode is set (screen size, refresh rate, ps->fullScreen)
    patch::SetUChar(0x7462FF, 0xEB);  // je -> jmp

    // WinMain: don't restyle the window to WS_POPUP after RenderWare is initialized
    patch::SetRaw(0x74887B, (void*)"\xEB\x5D", 2);  // jmp 0x7488DA
}
