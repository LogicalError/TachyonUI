#include <gui_core/graphics/d3d12/d3d12_graphics_context.h>

#include <gui_core/graphics/d3d12/d3d12_device.h>
#include <gui_core/graphics/d3d12/d3d12_window_context.h>
#include <gui_core/graphics/gui_gpu_choice.h>
#include <gui_core/graphics/gui_primitive_renderer.h>
#include <gui_core/diagnostics/gui_logging.h>

#include <format>
#include <vector>

static constexpr tachyon::Diagnostics::LogCategory kLogD3D12 = tachyon::Diagnostics::MakeLogCategory("d3d12");

namespace tachyon::d3d12
{
    std::string HResultText(HRESULT hr)
    {
        return std::format("0x{:08X}", static_cast<uint32_t>(hr));
    }

    std::string FromWide(const wchar_t* text)
    {
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        if (bytes <= 1)
            return {};
        std::string out(size_t(bytes - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), bytes, nullptr, nullptr);
        return out;
    }

    void D3D12GraphicsContext::Impl::Wait(ID3D12Fence* fence, UINT64 value)
    {
        if (fence == nullptr || fence->GetCompletedValue() >= value)
            return;
        fence->SetEventOnCompletion(value, idleEvent);
        WaitForSingleObject(idleEvent, INFINITE);
    }

    void D3D12GraphicsContext::Impl::WaitIdle()
    {
        if (!queue)
            return;
        Signal(idleFence.Get(), ++idleValue);
        Wait(idleFence.Get(), idleValue);
    }

    namespace
    {
        // The debug layer's messages, to the test's callback or the log.
        void CALLBACK OnDebugMessage(D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID,
                                     LPCSTR description, void* context)
        {
            const DebugConfig& debug = static_cast<D3D12GraphicsContext::Impl*>(context)->debug;
            const DebugSeverity level = severity <= D3D12_MESSAGE_SEVERITY_ERROR ? DebugSeverity::Error
                                      : severity == D3D12_MESSAGE_SEVERITY_WARNING ? DebugSeverity::Warning
                                                                                   : DebugSeverity::Info;
            if (debug.callback != nullptr)
                debug.callback(level, description, debug.userData);
            else if (level == DebugSeverity::Error)
                TACHYON_LOG_ERROR(kLogD3D12, "{}", description);
            else if (level == DebugSeverity::Warning)
                TACHYON_LOG_WARNING(kLogD3D12, "{}", description);
        }

        // A GPU the backend can render with: its adapter, and what kind it is.
        struct Candidate
        {
            ComPtr<IDXGIAdapter1> adapter;
            GpuInfo               info;
        };

        // Whether the adapter runs the shaders: Shader Model 6.6 and resource binding tier 3,
        // for their descriptor heap indexing. Its kind comes from the device too.
        bool Suitable(IDXGIAdapter1* adapter, const DXGI_ADAPTER_DESC1& desc, GpuKind& kind)
        {
            ComPtr<ID3D12Device> device;
            if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))))
                return false;
            D3D12_FEATURE_DATA_SHADER_MODEL model{ D3D_SHADER_MODEL_6_6 };
            if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model)))
                || model.HighestShaderModel < D3D_SHADER_MODEL_6_6)
                return false;
            D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
            if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))
                || options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_3)
                return false;
            D3D12_FEATURE_DATA_ARCHITECTURE architecture{};
            device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture));
            kind = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ? GpuKind::Cpu
                 : architecture.UMA                                ? GpuKind::Integrated
                                                                   : GpuKind::Discrete;
            return true;
        }
    }

    D3D12GraphicsContext::D3D12GraphicsContext() : m_impl(std::make_unique<Impl>())
    {
    }

    D3D12GraphicsContext::~D3D12GraphicsContext()
    {
        Cleanup();
    }

    std::string_view D3D12GraphicsContext::GetDeviceName() const
    {
        return m_impl->deviceName;
    }

    void D3D12GraphicsContext::SetGpuPreference(std::string_view preference)
    {
        m_impl->gpuPreference = std::string(preference);
    }

    void D3D12GraphicsContext::SetDebugConfig(const DebugConfig& config)
    {
        m_impl->debug = config;
    }

    Result<void> D3D12GraphicsContext::Initialize(IWindowBackend* windowBackend)
    {
        if (Result<void> base = NullGraphicsContext::Initialize(windowBackend); !base)
            return base;
        Impl& d = *m_impl;
        const auto fail = [this](ErrorCode code, std::string message) -> Result<void>
        {
            TACHYON_LOG_ERROR(kLogD3D12, "{}", message);
            Cleanup();
            return Error{ code, std::move(message) };
        };

        // The debug layer: for a test's callback, and in debug builds.
#if defined(NDEBUG)
        const bool wantDebug = d.debug.callback != nullptr;
#else
        const bool wantDebug = true;
#endif
        if (wantDebug)
        {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
            {
                debug->EnableDebugLayer();
                d.debugLayer = true;
            }
            else
                TACHYON_LOG_WARNING(kLogD3D12, "no debug layer (Windows' Graphics Tools aren't installed)");
        }

        if (HRESULT hr = CreateDXGIFactory2(d.debugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&d.factory)); FAILED(hr))
            return fail(ErrorCode::Unsupported, std::format("Direct3D 12: no DXGI factory ({})", HResultText(hr)));
        BOOL tearing = FALSE;
        d.tearing = SUCCEEDED(d.factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) && tearing;

        // The GPUs, the fastest first, and the first that runs the shaders unless the app or
        // TACHYON_GPU asks for another.
        std::vector<Candidate> candidates;
        TACHYON_LOG_INFO(kLogD3D12, "Detected Direct3D 12 GPUs:");
        for (UINT i = 0;; ++i)
        {
            ComPtr<IDXGIAdapter1> adapter;
            if (d.factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND)
                break;
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            GpuKind kind = GpuKind::Other;
            const std::string name = FromWide(desc.Description);
            if (!Suitable(adapter.Get(), desc, kind))
            {
                TACHYON_LOG_INFO(kLogD3D12, "  -: {} (no Shader Model 6.6 with resource binding tier 3)", name);
                continue;
            }
            TACHYON_LOG_INFO(kLogD3D12, "  {}: {} ({})", candidates.size(), name, GpuKindName(kind));
            candidates.push_back({ adapter, { name, kind } });
        }
        if (candidates.empty())
            return fail(ErrorCode::Unsupported, "Direct3D 12: no GPU runs Shader Model 6.6 with resource binding tier 3 "
                                                "(Windows 11's D3D12 runtime has it)");
        std::vector<GpuInfo> infos;
        for (const Candidate& c : candidates)
            infos.push_back(c.info);
        const size_t chosen = ChooseGpu(infos, GpuPreference(d.gpuPreference)).value_or(0);
        d.adapter    = candidates[chosen].adapter;
        d.deviceName = candidates[chosen].info.name;
        TACHYON_LOG_INFO(kLogD3D12, "Using GPU {}: {} ({})", chosen, d.deviceName, GpuKindName(candidates[chosen].info.kind));

        if (HRESULT hr = D3D12CreateDevice(d.adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d.device)); FAILED(hr))
            return fail(ErrorCode::Failed, std::format("Direct3D 12: D3D12CreateDevice failed ({})", HResultText(hr)));
        if (d.debugLayer && SUCCEEDED(d.device.As(&d.infoQueue)))
            d.infoQueue->RegisterMessageCallback(&OnDebugMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, &d, &d.messageCallback);

        const D3D12_COMMAND_QUEUE_DESC queueDesc{ D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
                                                  D3D12_COMMAND_QUEUE_FLAG_NONE, 0 };
        if (HRESULT hr = d.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&d.queue)); FAILED(hr))
            return fail(ErrorCode::Failed, std::format("Direct3D 12: CreateCommandQueue failed ({})", HResultText(hr)));
        if (HRESULT hr = d.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&d.idleFence)); FAILED(hr))
            return fail(ErrorCode::Failed, std::format("Direct3D 12: CreateFence failed ({})", HResultText(hr)));
        d.idleEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return {};
    }

    void D3D12GraphicsContext::Cleanup()
    {
        Impl& d = *m_impl;
        d.WaitIdle();
        if (d.infoQueue && d.messageCallback != 0)
            d.infoQueue->UnregisterMessageCallback(d.messageCallback);
        d.messageCallback = 0;
        d.infoQueue.Reset();
        d.idleFence.Reset();
        d.queue.Reset();
        d.device.Reset();
        d.adapter.Reset();
        d.factory.Reset();
        if (d.idleEvent != nullptr)
            CloseHandle(d.idleEvent);
        d.idleEvent = nullptr;
        d.deviceName.clear();
        NullGraphicsContext::Cleanup();
    }

    IWindowContext* D3D12GraphicsContext::CreateWindowContext(void* nativeWindowHandle, OSWindowHandle osHandle, ID id, int2 size,
                                                              WindowFlags windowFlags, OnGuiFunc onGui)
    {
        if (nativeWindowHandle == nullptr || !m_impl->device)
            return NullGraphicsContext::CreateWindowContext(nativeWindowHandle, osHandle, id, size, windowFlags, std::move(onGui));
        return D3D12WindowContext::Create(*m_impl, static_cast<HWND>(nativeWindowHandle), osHandle, id, size, windowFlags);
    }

    void D3D12GraphicsContext::DestroyWindowContext(IWindowContext* context)
    {
        if (auto* window = dynamic_cast<D3D12WindowContext*>(context))
        {
            if (guiRenderer != nullptr)
                static_cast<PrimitiveRenderer*>(guiRenderer)->ReleaseTarget(window->GetID());
            delete window;
            return;
        }
        NullGraphicsContext::DestroyWindowContext(context);
    }

    void D3D12GraphicsContext::WaitIdle()
    {
        m_impl->WaitIdle();
    }
}
