#pragma once

#include <cstdint>
#include <string>

// debug instances connected to the stand hub (CStandAgent): the frames for its live view and screenshots. On demand
// only (the hub asks again every 2 s, the frames stop 5 s after the last ask): before RwCameraShowRaster presents a
// frame, the back buffer is scaled down on the GPU, reads it back and writes it into the shared memory "Local\CoopStandFrame_<stand>_<n>"
// (StandFrameHeader + BGRX rows). The picture does not depend on the window: hidden or covered, it is the game's own
class CStandFrames
{
public:
    static constexpr uint32_t MAGIC = 0x31465343; // "CSF1"
    static constexpr int MAX_WIDTH = 1920;
    static constexpr int MAX_HEIGHT = 1080;

    // the writer makes seq odd while it writes: a reader copies, checks seq again and retries when it changed
    struct StandFrameHeader
    {
        uint32_t magic;
        volatile long seq;
        uint32_t width;
        uint32_t height;
        uint32_t stride;
        uint32_t tick;
    };

    static void Init(const std::string& stand, int index);
    // agent thread: fps 0 stops
    static void SetDemand(int fps, int width);
    // for the agent state: the demand and the counters, a stuck capture shows here
    static std::string StateJson();
};
