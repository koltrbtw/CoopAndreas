#pragma once

// debug instances: a game that stops making frames (the hang on the loading screen of the stand, no dump of it so far)
// leaves a minidump with the thread stacks in CoopAndreas_crashes
class CHangWatchdog
{
public:
    static void Start();
};
