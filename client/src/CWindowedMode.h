#pragma once

// switches RenderWare to its windowed video mode and lays out the windows side by side,
// so two local instances (--coopd0 / --coopd1) can run at the same time
class CWindowedMode
{
public:
    static void Init();
};
