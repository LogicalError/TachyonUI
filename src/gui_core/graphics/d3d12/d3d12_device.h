#pragma once

// Inside the Direct3D 12 backend: the device and its queue, shared by the graphics context
// and its window contexts.

#include <gui_core/graphics/d3d12/d3d12_common.h>
#include <gui_core/graphics/d3d12/d3d12_graphics_context.h>

#include <string>

namespace tachyon::d3d12
{
    struct D3D12GraphicsContext::Impl
    {
        DebugConfig debug;
        std::string gpuPreference;
        std::string deviceName;
        bool        debugLayer = false;   ///< the debug layer is on: its messages go to `debug`
        bool        tearing    = false;   ///< presenting without vsync is allowed (variable refresh)

        ComPtr<IDXGIFactory6>      factory;
        ComPtr<IDXGIAdapter1>      adapter;
        ComPtr<ID3D12Device>       device;
        ComPtr<ID3D12CommandQueue> queue;
        ComPtr<ID3D12InfoQueue1>   infoQueue;
        DWORD                      messageCallback = 0;

        /// WaitIdle's: a value signalled after all work submitted so far.
        ComPtr<ID3D12Fence> idleFence;
        UINT64              idleValue = 0;
        HANDLE              idleEvent = nullptr;

        /// Signals `fence` to `value` on the queue once the work submitted so far is done.
        void Signal(ID3D12Fence* fence, UINT64 value) { queue->Signal(fence, value); }
        /// Waits on the CPU until `fence` reaches `value`.
        void Wait(ID3D12Fence* fence, UINT64 value);
        void WaitIdle();
    };
}
