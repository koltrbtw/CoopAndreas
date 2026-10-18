#include "stdafx.h"
#include "CWindowedMode.h"

// sizes the game window to a cell of the primary monitor work area (16:9 client area): two instances side by side,
// up to four in a 2x2 grid, up to eight in 3 columns (--coopslots tells how many the stand runs)
static void PlaceWindow(HWND hWnd, int& clientWidth, int& clientHeight)
{
    const LONG style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE | WS_CLIPSIBLINGS;

    RECT workArea{};
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);

    RECT frame = {0, 0, 0, 0};
    AdjustWindowRect(&frame, style, FALSE);
    const int frameWidth = frame.right - frame.left;
    const int frameHeight = frame.bottom - frame.top;

    const int slots = CCore::ms_nRunSlots;
    const int columns = slots <= 4 ? 2 : 3;
    const int rows = (slots + columns - 1) / columns;
    const int column = CCore::ms_nRunIndex % columns;
    const int row = (CCore::ms_nRunIndex / columns) % rows;

    const int cellWidth = (workArea.right - workArea.left) / columns;
    const int cellHeight = (workArea.bottom - workArea.top) / rows;

    clientWidth = cellWidth - frameWidth;
    clientHeight = clientWidth * 9 / 16;
    if (clientHeight > cellHeight - frameHeight)
    {
        clientHeight = cellHeight - frameHeight;
        clientWidth = clientHeight * 16 / 9;
    }

    const int outerWidth = clientWidth + frameWidth;
    const int outerHeight = clientHeight + frameHeight;

    // two instances keep the old layout: the second one at the right edge
    const int x = slots <= 2 && column == 1 ? workArea.right - outerWidth : workArea.left + column * cellWidth;
    const int y = workArea.top + row * cellHeight;

    SetWindowLongW(hWnd, GWL_STYLE, style);
    SetWindowLongW(hWnd, GWL_EXSTYLE, 0);
    // the z-order is kept: a window raised over the user's ones is as bad as a stolen focus
    SetWindowPos(hWnd, nullptr, x, y, outerWidth, outerHeight,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOZORDER);
}

// a stand starts several instances while the user works in other windows: the game must never take the focus or the
// cursor itself. Only a click on its window (or a tool asking for it) activates it. The wrappers replace the exe's
// user32 imports, so the direct calls and the 0x81E5xx jump thunks both go through them

static bool IsOurWindowFocused()
{
    return RsGlobal.ps && RsGlobal.ps->window && GetForegroundWindow() == RsGlobal.ps->window;
}

// the "already running" check (0x746918, 0x74692C) and the window creation
static BOOL WINAPI SetForegroundWindow_Never(HWND)
{
    return TRUE;
}

// WinMain shows the new window with SW_SHOWNORMAL (0x748982): that activates it
static BOOL WINAPI ShowWindow_NoActivate(HWND hWnd, int nCmdShow)
{
    switch (nCmdShow)
    {
    case SW_SHOWNORMAL:
    case SW_SHOWDEFAULT:
    case SW_RESTORE:
        nCmdShow = SW_SHOWNOACTIVATE;
        break;
    case SW_SHOW:
        nCmdShow = SW_SHOWNA;
        break;
    default:
        break;
    }
    return ShowWindow(hWnd, nCmdShow);
}

static BOOL WINAPI SetWindowPos_NoActivate(HWND hWnd, HWND hWndInsertAfter, int x, int y, int cx, int cy, UINT flags)
{
    return SetWindowPos(hWnd, hWndInsertAfter, x, y, cx, cy, flags | SWP_NOACTIVATE);
}

// CMenuManager::CentreMousePointer (0x57C59B) and psMouseSetPos (0x74542D) centre the cursor in the window; with
// ForegroundApp forced to 1 they ran in a background window too and pulled the cursor away from the user
static BOOL WINAPI SetCursorPos_OnlyWhenFocused(int x, int y)
{
    return IsOurWindowFocused() ? SetCursorPos(x, y) : TRUE;
}

// the clip to the window rect (WM_ACTIVATE etc.), a release (nullptr) always goes through
static BOOL WINAPI ClipCursor_OnlyWhenFocused(const RECT* pRect)
{
    return !pRect || IsOurWindowFocused() ? ClipCursor(pRect) : TRUE;
}

static void PatchFocusImports()
{
    patch::SetPointer(0x8582B8, (void*)SetForegroundWindow_Never);
    patch::SetPointer(0x858278, (void*)ShowWindow_NoActivate);
    patch::SetPointer(0x8582E0, (void*)SetWindowPos_NoActivate);
    patch::SetPointer(0x858300, (void*)SetCursorPos_OnlyWhenFocused);
    patch::SetPointer(0x858294, (void*)ClipCursor_OnlyWhenFocused);
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
    PatchFocusImports();

    // skip the device selection dialog shown on multi-monitor setups, use the saved video mode path
    patch::SetUChar(0x746225, 0xEB);  // jle -> jmp

    patch::RedirectCall(0x7462EA, RwEngineSetVideoMode_Hook);

    // skip the fullscreen-only setup after the video mode is set (screen size, refresh rate, ps->fullScreen)
    patch::SetUChar(0x7462FF, 0xEB);  // je -> jmp

    // WinMain: don't restyle the window to WS_POPUP after RenderWare is initialized
    patch::SetRaw(0x74887B, (void*)"\xEB\x5D", 2);  // jmp 0x7488DA
}
