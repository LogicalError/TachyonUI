#pragma once

// Inside the Direct3D 12 backend only: the Windows and D3D12 headers, without their macros
// leaking into the library's names (CreateWindow, min and max).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef CreateWindow

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <string>

namespace tachyon::d3d12
{
    using Microsoft::WRL::ComPtr;

    /// A failed call's HRESULT as text, for errors and logs ("0x887A0005").
    std::string HResultText(HRESULT hr);

    /// UTF-16 from Windows (an adapter's name) as UTF-8.
    std::string FromWide(const wchar_t* text);
}
