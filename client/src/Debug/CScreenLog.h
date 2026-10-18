#pragma once

// stand instances (--coopd*) only: what this player sees on the screen, for the mission tests to check by the log
// instead of the eye. Every change is a "screen:" line: a subtitle, a help, a big message, widescreen, the control, the
// fixed camera, a cutscene, a counter. `hud` (CDebugCommands) logs the whole state now, with the blips and the
// checkpoint
class CScreenLog
{
public:
    static void Process();
    static void DumpState();
};
