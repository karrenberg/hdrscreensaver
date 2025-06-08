// DirectX10BitBackend.h
// Concrete implementation using DirectX 11 for 10-bit HDR output
// This backend creates a borderless window and uses DXGI swapchain with 10-bit pixel format for HDR rendering.
// It loads the image using Skia, uploads it as a texture, and presents in 10-bit color if supported.
//
// NOTE: This is a minimal, extensible starting point. Error handling and resource management are included.
//
// Dependencies: d3d11.lib, dxgi.lib, Skia (for image loading)
#pragma once
#define NOMINMAX
#include "IImageBackend.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <string>
#include <iostream>

class DirectX10BitBackend : public IImageBackend {
public:
    DirectX10BitBackend();
    ~DirectX10BitBackend();
    // Uploads a raw image buffer (RGBA F16, Rec.2020 linear) to the GPU for display
    bool UploadImageBuffer(const void* pixelData, int width, int height, int rowBytes);
    // Starts the HDR window and render loop (call after uploading the image buffer)
    HBITMAP LoadImage(const std::wstring& imagePath) override;
    // Explicitly create the borderless window and initialize D3D device/context
    bool InitializeWindowAndDevice();
    // Overload: Initialize with external HWND and size (for preview mode)
    bool InitializeWindowAndDevice(HWND externalHwnd, int width, int height, bool forceSDR = false);
    HWND GetWindowHandle() const { return m_hWnd; }
    // Helper to present the image
    void Present();
private:
    // Helper to initialize DirectX 11 device and swapchain
    bool InitD3D(HWND hWnd, UINT width, UINT height, bool forceSDR = false);
    void Cleanup();
    // DirectX resources
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_imageTexture;
    // New: for correct texture/aspect handling
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vb;
    int m_lastImgW = 0, m_lastImgH = 0, m_lastWinW = 0, m_lastWinH = 0;
    UINT m_width = 0;
    UINT m_height = 0;
    HWND m_hWnd = nullptr;
    bool m_exactFit = false; // If true, disables aspect ratio scaling (for preview mode)
};
