#include "stdafx.h"
#include "CStandFrames.h"
#include <atomic>

namespace
{
HANDLE g_hMapping = nullptr;
uint8_t* g_pShared = nullptr;

std::atomic<int> g_nFps = 0;
std::atomic<int> g_nWidth = 0;
std::atomic<DWORD> g_nDemandUntil = 0;
DWORD g_nLastCapture = 0;
std::atomic<unsigned> g_nShows = 0;
std::atomic<unsigned> g_nCaptures = 0;

IDirect3DSurface9* g_pScaled = nullptr; // D3DPOOL_DEFAULT render target, released on a device loss
IDirect3DSurface9* g_pSystem = nullptr; // its copy in the system memory, locked for the read
UINT g_nSurfaceWidth = 0, g_nSurfaceHeight = 0;

uintptr_t g_pOriginalShowRaster = 0;
}

// a failed step is logged once: the frames stop silently otherwise
static void LogOnce(const char* step, HRESULT hr)
{
    static std::string logged;
    if (logged.find(step) == std::string::npos)
    {
        logged += step;
        logger::warn("stand frames: %s failed (0x%08lx)", step, static_cast<unsigned long>(hr));
    }
}

static void ReleaseSurfaces()
{
    if (g_pScaled)
    {
        g_pScaled->Release();
        g_pScaled = nullptr;
    }
    if (g_pSystem)
    {
        g_pSystem->Release();
        g_pSystem = nullptr;
    }
    g_nSurfaceWidth = g_nSurfaceHeight = 0;
}

static void Capture(IDirect3DDevice9* pDevice)
{
    const DWORD now = GetTickCount();
    const int fps = g_nFps;
    if (fps <= 0 || now > g_nDemandUntil || now - g_nLastCapture < static_cast<DWORD>(1000 / fps))
    {
        return;
    }
    g_nLastCapture = now;

    IDirect3DSurface9* pBackBuffer = nullptr;
    if (HRESULT hr = pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer); FAILED(hr))
    {
        LogOnce("GetBackBuffer", hr);
        return;
    }

    D3DSURFACE_DESC desc{};
    pBackBuffer->GetDesc(&desc);

    // the asked width (0 = the game's), never above the game's or the shared memory; even sizes for the encoders
    UINT width = g_nWidth > 0 ? std::min<UINT>(g_nWidth, desc.Width) : desc.Width;
    width = std::min<UINT>(width, CStandFrames::MAX_WIDTH);
    UINT height = std::min<UINT>(desc.Height * width / desc.Width, CStandFrames::MAX_HEIGHT);
    width &= ~1u;
    height &= ~1u;

    if (width != g_nSurfaceWidth || height != g_nSurfaceHeight)
    {
        ReleaseSurfaces();
        HRESULT hr = pDevice->CreateRenderTarget(width, height, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE,
            &g_pScaled, nullptr);
        if (SUCCEEDED(hr))
        {
            hr = pDevice->CreateOffscreenPlainSurface(width, height, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &g_pSystem,
                nullptr);
        }
        if (FAILED(hr))
        {
            LogOnce("CreateSurfaces", hr);
            ReleaseSurfaces();
            pBackBuffer->Release();
            return;
        }
        g_nSurfaceWidth = width;
        g_nSurfaceHeight = height;
    }

    // StretchRect resolves a multisampled back buffer and converts the format too
    HRESULT hr = pDevice->StretchRect(pBackBuffer, nullptr, g_pScaled, nullptr, D3DTEXF_LINEAR);
    pBackBuffer->Release();
    if (FAILED(hr))
    {
        LogOnce("StretchRect", hr);
        return;
    }
    if (hr = pDevice->GetRenderTargetData(g_pScaled, g_pSystem); FAILED(hr))
    {
        LogOnce("GetRenderTargetData", hr);
        return;
    }

    D3DLOCKED_RECT locked{};
    if (hr = g_pSystem->LockRect(&locked, nullptr, D3DLOCK_READONLY); FAILED(hr))
    {
        LogOnce("LockRect", hr);
        return;
    }

    auto* pHeader = reinterpret_cast<CStandFrames::StandFrameHeader*>(g_pShared);
    uint8_t* pPixels = g_pShared + sizeof(CStandFrames::StandFrameHeader);
    InterlockedIncrement(&pHeader->seq);
    pHeader->width = width;
    pHeader->height = height;
    pHeader->stride = width * 4;
    pHeader->tick = now;
    for (UINT y = 0; y < height; y++)
    {
        memcpy(pPixels + y * width * 4, static_cast<uint8_t*>(locked.pBits) + y * locked.Pitch, width * 4);
    }
    InterlockedIncrement(&pHeader->seq);
    g_nCaptures++;

    g_pSystem->UnlockRect();
}

// psCameraShowRaster -> RwCameraShowRaster (0x745265, its only caller): the frame is complete (the scene ended),
// the next step presents it. Hooking IDirect3DDevice9::Present in the vtable caught only the loading screens: the
// game frames went past it
static RwCamera* __cdecl RwCameraShowRaster_Hook(RwCamera* pCamera, void* pDev, RwUInt32 flags)
{
    g_nShows++;
    Capture(static_cast<IDirect3DDevice9*>(RwD3D9GetCurrentD3DDevice()));
    return plugin::CallAndReturnDyn<RwCamera*, RwCamera*, void*, RwUInt32>(g_pOriginalShowRaster, pCamera, pDev, flags);
}

void CStandFrames::Init(const std::string& stand, int index)
{
    const DWORD size = sizeof(StandFrameHeader) + MAX_WIDTH * MAX_HEIGHT * 4;
    std::string name = "Local\\CoopStandFrame_" + stand + "_" + std::to_string(index);
    g_hMapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size, name.c_str());
    g_pShared = g_hMapping ? static_cast<uint8_t*>(MapViewOfFile(g_hMapping, FILE_MAP_ALL_ACCESS, 0, 0, size)) : nullptr;
    if (!g_pShared)
    {
        logger::warn("stand frames: no shared memory %s (%lu)", name.c_str(), GetLastError());
        return;
    }
    reinterpret_cast<StandFrameHeader*>(g_pShared)->magic = MAGIC;

    g_pOriginalShowRaster = injector::GetBranchDestination(0x745265).as_int();
    patch::RedirectCall(0x745265, RwCameraShowRaster_Hook);
    Events::d3dLostEvent += [] { ReleaseSurfaces(); };
    Events::shutdownRwEvent += [] { ReleaseSurfaces(); };
}

std::string CStandFrames::StateJson()
{
    const DWORD now = GetTickCount();
    const long left = static_cast<long>(g_nDemandUntil - now);
    char json[160];
    sprintf_s(json, "{\"fps\":%d,\"width\":%d,\"demandMs\":%ld,\"shows\":%u,\"captures\":%u}",
        static_cast<int>(g_nFps), static_cast<int>(g_nWidth), left > 0 ? left : 0, static_cast<unsigned>(g_nShows),
        static_cast<unsigned>(g_nCaptures));
    return json;
}

void CStandFrames::SetDemand(int fps, int width)
{
    if (fps > 0 && g_nFps == 0)
    {
        logger::info("stand frames: %d fps, width %d", fps, width);
    }
    g_nFps = std::clamp(fps, 0, 30);
    g_nWidth = std::clamp(width, 0, MAX_WIDTH);
    g_nDemandUntil = GetTickCount() + 5000;
}
