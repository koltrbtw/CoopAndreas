#pragma once

// switches RenderWare to its windowed video mode and lays out the windows in a grid, so several local instances
// (--coopd<N>) can run at the same time without taking the focus or the cursor from the user
class CWindowedMode
{
public:
    static void Init();
};
