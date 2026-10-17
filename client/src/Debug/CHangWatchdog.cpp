#include "stdafx.h"
#include "CHangWatchdog.h"
#include <DbgHelp.h>

static constexpr DWORD HANG_TIME = 60000;  // ms without a game frame, longer than any loading of the stand
static constexpr DWORD CHECK_INTERVAL = 1000;

static DWORD ms_nMainThreadId = 0;

static void WriteHangDump()
{
    CreateDirectoryA("CoopAndreas_crashes", NULL);

    SYSTEMTIME time;
    GetLocalTime(&time);
    char szFileName[MAX_PATH];
    sprintf_s(szFileName, "CoopAndreas_crashes\\%04d-%02d-%02d_%02d-%02d-%02d_hang.dmp", time.wYear, time.wMonth, time.wDay,
        time.wHour, time.wMinute, time.wSecond);

    HANDLE hFile = CreateFileA(szFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return;
    }

    // the stacks and what they point to are enough to see where the main thread waits, a full dump is ~500 MB
    auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory |
                                           MiniDumpWithDataSegs | MiniDumpWithHandleData);
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, type, NULL, NULL, NULL);
    CloseHandle(hFile);

    logger::warn("no game frame for %u s: %s (main thread %u)", HANG_TIME / 1000, szFileName, ms_nMainThreadId);
}

static void WatchdogThread()
{
    unsigned int nLastFrame = CTimer::m_FrameCounter;
    DWORD nLastChange = GetTickCount();

    while (true)
    {
        Sleep(CHECK_INTERVAL);

        // a minimized window or a pause in the debugger makes no frames either
        HWND hWnd = RsGlobal.ps ? RsGlobal.ps->window : NULL;
        if (CTimer::m_FrameCounter != nLastFrame || (hWnd && IsIconic(hWnd)) || IsDebuggerPresent())
        {
            nLastFrame = CTimer::m_FrameCounter;
            nLastChange = GetTickCount();
            continue;
        }

        if (GetTickCount() - nLastChange >= HANG_TIME)
        {
            WriteHangDump();
            return;  // one dump a session
        }
    }
}

void CHangWatchdog::Start()
{
    ms_nMainThreadId = GetCurrentThreadId();
    std::thread(WatchdogThread).detach();
}
