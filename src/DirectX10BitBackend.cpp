// DirectX10BitBackend.cpp
// Implements true HDR (10-bit/channel) image display using DirectX 11.
// This backend creates a borderless window, initializes a DXGI swapchain with a 10-bit or 16-bit floating point format,
// and presents a pre-uploaded image in HDR.
//
// Limitations: This backend does not return an HBITMAP (returns nullptr). It is designed for direct display only.
//
// Author: GitHub Copilot

#include "DirectX10BitBackend.h"
#include "Logger.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <windows.h>
#include <iostream>
#include <vector>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

// Declare global shutdown flag from main.cpp
extern volatile bool g_shutdownRequested;

using Microsoft::WRL::ComPtr;

DirectX10BitBackend::DirectX10BitBackend() {}
DirectX10BitBackend::~DirectX10BitBackend() { Cleanup(); }

// Helper: Release all DX resources
void DirectX10BitBackend::Cleanup() {
    m_rtv.Reset();
    m_imageTexture.Reset();
    m_swapChain.Reset();
    m_context.Reset();
    m_device.Reset();
}

// Helper: Initialize D3D11 device and 10-bit swapchain for HDR
bool DirectX10BitBackend::InitD3D(HWND hWnd, UINT width, UINT height, bool forceSDR) {
    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width = width;
    scd.Height = height;
    scd.Format = forceSDR ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT;
    scd.Stereo = FALSE;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.Scaling = DXGI_SCALING_STRETCH;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<IDXGIFactory2> factory;

    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory2), (void**)factory.GetAddressOf());
    if (FAILED(hr)) {
        LOG_MSG(L"[ERROR] DXGI factory creation failed. HRESULT: 0x" + std::wstring(1, L"0123456789ABCDEF"[(hr>>28)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>24)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>20)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>16)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>12)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>8)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>4)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr)&0xF]));
        return false;
    }
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevels, 1, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf());
    if (FAILED(hr)) {
        LOG_MSG(L"[ERROR] D3D11 device creation failed. HRESULT: 0x" + std::wstring(1, L"0123456789ABCDEF"[(hr>>28)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>24)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>20)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>16)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>12)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>8)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>4)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr)&0xF]));
        return false;
    }
    hr = factory->CreateSwapChainForHwnd(device.Get(), hWnd, &scd, nullptr, nullptr, swapChain.GetAddressOf());
    if (FAILED(hr)) {
        LOG_MSG(L"[ERROR] Swapchain creation failed. HRESULT: 0x" + std::wstring(1, L"0123456789ABCDEF"[(hr>>28)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>24)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>20)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>16)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>12)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>8)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>4)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr)&0xF]));
        LOG_MSG(L"[DEBUG] Swapchain params: width=" + std::to_wstring(width) + L", height=" + std::to_wstring(height) + L", format=" + (forceSDR ? L"DXGI_FORMAT_R8G8B8A8_UNORM" : L"DXGI_FORMAT_R16G16B16A16_FLOAT"));
        return false;
    }
    // Create render target view
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf());
    if (FAILED(hr)) {
        LOG_MSG(L"[ERROR] GetBuffer failed. HRESULT: 0x" + std::wstring(1, L"0123456789ABCDEF"[(hr>>28)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>24)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>20)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>16)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>12)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>8)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>4)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr)&0xF]));
        return false;
    }
    ComPtr<ID3D11RenderTargetView> rtv;
    hr = device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtv.GetAddressOf());
    if (FAILED(hr)) {
        LOG_MSG(L"[ERROR] CreateRenderTargetView failed. HRESULT: 0x" + std::wstring(1, L"0123456789ABCDEF"[(hr>>28)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>24)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>20)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>16)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>12)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>8)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr>>4)&0xF]) +
            std::wstring(1, L"0123456789ABCDEF"[(hr)&0xF]));
        return false;
    }
    m_device = device;
    m_context = context;
    m_swapChain = swapChain;
    m_rtv = rtv;
    m_width = width;
    m_height = height;
    m_hWnd = hWnd;
    LOG_MSG(L"[LOG] D3D11 device and swapchain initialized. Format: ", (forceSDR ? L"SDR (R8G8B8A8)" : L"HDR (R16G16B16A16)"));
    return true;
}

// Helper: Uploads a raw image buffer to a DirectX texture (no image loading or color management)
bool DirectX10BitBackend::UploadImageBuffer(const void* pixelData, int width, int height, int rowBytes) {
    if (!pixelData) {
        LOG_MSG("[ERROR] [DX11] pixelData is null!");
        return false;
    }
    if (width <= 0 || height <= 0) {
        LOG_MSG("[ERROR] [DX11] Invalid image dimensions!");
        return false;
    }
    if (rowBytes <= 0) {
        LOG_MSG("[ERROR] [DX11] Invalid rowBytes!");
        return false;
    }
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixelData;
    initData.SysMemPitch = rowBytes;
    ComPtr<ID3D11Texture2D> tex;
    HRESULT hr = m_device->CreateTexture2D(&desc, &initData, tex.GetAddressOf());
    if (FAILED(hr)) {
        LOG_MSG("[ERROR] [DX11] Failed to create DirectX texture from image buffer.");
        return false;
    }
    m_imageTexture = tex;
    // Reset static SRV and vertex buffer in Present()
    m_srv.Reset();
    m_sampler.Reset();
    m_vb.Reset();
    m_lastImgW = 0;
    m_lastImgH = 0;
    m_lastWinW = 0;
    m_lastWinH = 0;
    return true;
}

// Helper: Present the image (render textured quad with the loaded image)
// The pixel shader does not perform any color conversion. DirectX only displays the values provided by Skia.
void DirectX10BitBackend::Present() {
    // Check if we have a valid image texture
    if (!m_imageTexture) {
        LOG_MSG("[LOG] DX11: No image texture, clearing to background color.");
        float clearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);
        m_swapChain->Present(1, 0);
        return;
    }
    // --- Fullscreen textured quad rendering ---
    // --- Shader setup (static, only once) ---
    static Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
    static Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader;
    static Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout;
    static bool shadersInitialized = false;
    if (!shadersInitialized) {
        LOG_MSG("[LOG] DX11: Compiling shaders... (no color conversion, Skia handles all color management)");
        const char* vsCode = R"(
            struct VS_INPUT { float3 pos : POSITION; float2 uv : TEXCOORD0; };
            struct PS_INPUT { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
            PS_INPUT main(VS_INPUT input) {
                PS_INPUT output;
                output.pos = float4(input.pos, 1.0);
                output.uv = input.uv;
                return output;
            }
        )";
        // Pixel shader: sample from texture, no color conversion or gamma correction (Skia handles all color management)
        const char* psCode = R"(
            Texture2D tex : register(t0);
            SamplerState samp : register(s0);
            float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
                return tex.Sample(samp, uv);
            }
        )";
        ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;
        HRESULT hr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
        if (FAILED(hr)) { LOG_MSG("[ERROR] DX11: Vertex shader compile failed."); return; }
        hr = D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
        if (FAILED(hr)) { LOG_MSG("[ERROR] DX11: Pixel shader compile failed."); return; }
        m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vertexShader);
        m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pixelShader);
        // Input layout
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        m_device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout);
        shadersInitialized = true;
        LOG_MSG("[LOG] DX11: Shaders compiled and input layout created (no color conversion).");
    }

    // --- Fullscreen quad vertex buffer (now using member variables) ---
    struct Vertex { float pos[3]; float uv[2]; };
    int imgW = 0, imgH = 0;
    if (m_imageTexture) {
        D3D11_TEXTURE2D_DESC texDesc = {};
        m_imageTexture->GetDesc(&texDesc);
        imgW = (int)texDesc.Width;
        imgH = (int)texDesc.Height;
    }
    int winW = (int)m_width, winH = (int)m_height;
    Vertex quad[6];
    if (imgW != m_lastImgW || imgH != m_lastImgH || winW != m_lastWinW || winH != m_lastWinH) {
        float scaleX = 1.0f, scaleY = 1.0f;
        if (!m_exactFit) {
            float imgAspect = imgW / (float)imgH;
            float winAspect = winW / (float)winH;
            if (imgAspect > winAspect) {
                scaleY = winAspect / imgAspect;
            } else {
                scaleX = imgAspect / winAspect;
            }
        }
        quad[0] = { {-scaleX, -scaleY, 0}, {0, 1} };
        quad[1] = { {-scaleX,  scaleY, 0}, {0, 0} };
        quad[2] = { { scaleX,  scaleY, 0}, {1, 0} };
        quad[3] = { {-scaleX, -scaleY, 0}, {0, 1} };
        quad[4] = { { scaleX,  scaleY, 0}, {1, 0} };
        quad[5] = { { scaleX, -scaleY, 0}, {1, 1} };
        m_lastImgW = imgW; m_lastImgH = imgH; m_lastWinW = winW; m_lastWinH = winH;
        m_vb.Reset();
    }
    if (!m_vb) {
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.ByteWidth = sizeof(quad);
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA init = { quad, 0, 0 };
        m_device->CreateBuffer(&bd, &init, &m_vb);
    }

    // --- Pipeline setup and draw ---
    UINT stride = sizeof(Vertex), offset = 0;
    m_context->IASetInputLayout(inputLayout.Get());
    m_context->IASetVertexBuffers(0, 1, m_vb.GetAddressOf(), &stride, &offset);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(vertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(pixelShader.Get(), nullptr, 0);
    // Bind image texture as SRV and set sampler
    if (!m_srv) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(m_imageTexture.Get(), &srvDesc, &m_srv);
        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter = D3D11_FILTER_ANISOTROPIC; //D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.MaxAnisotropy = 16;
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampDesc.MipLODBias     = -0.2f;   // bias toward sharper (higher-res) mips
        sampDesc.MinLOD = 0;
        sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
        m_device->CreateSamplerState(&sampDesc, &m_sampler);
    }
    m_context->PSSetShaderResources(0, 1, m_srv.GetAddressOf());
    m_context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
    // Set viewport to cover the entire render target
    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = static_cast<float>(m_width);
    vp.Height = static_cast<float>(m_height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);
    float clearColor[4] = { 0, 0, 0, 1 };
    m_context->ClearRenderTargetView(m_rtv.Get(), clearColor);
    m_context->Draw(6, 0);
    m_swapChain->Present(1, 0);
}

// Helper: Create window and initialize D3D device/context
bool DirectX10BitBackend::InitializeWindowAndDevice() {
    // Register a custom window class for mouse input
    static bool windowClassRegistered = false;
    if (!windowClassRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"HDRScreenSaverWindow";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        windowClassRegistered = true;
    }

    // Create a borderless window for HDR output that can receive mouse input
    HWND hWnd = CreateWindowExW(0, L"HDRScreenSaverWindow", L"HDR DX11 Output", WS_POPUP | WS_VISIBLE,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    LOG_MSG(L"[LOG] DX11: Created borderless window for HDR output. HWND=", reinterpret_cast<uintptr_t>(hWnd));

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);
    if (!InitD3D(hWnd, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN))) {
        LOG_MSG("[ERROR] Failed to initialize D3D for HDR output.");
        return false;
    }
    return true;
}

// Helper: Create window and initialize D3D device/context (preview mode)
bool DirectX10BitBackend::InitializeWindowAndDevice(HWND externalHwnd, int width, int height, bool forceSDR) {
    // Use the provided HWND and size for DirectX initialization (preview mode)
    m_hWnd = externalHwnd;
    m_width = width;
    m_height = height;
    m_exactFit = true; // Enable exact-fit for preview mode
    return InitD3D(m_hWnd, m_width, m_height, forceSDR);
}

// Loads an image and displays it in a 10-bit HDR window. Returns nullptr (no HBITMAP).
HBITMAP DirectX10BitBackend::LoadImage(const std::wstring& imagePath) {
    // Only run the message/render loop (window/device must be initialized first)
    LOG_MSG("[LOG] DX11: Entering main message/render loop.");
    MSG msg;
    while (!g_shutdownRequested) {
        while (PeekMessage(&msg, m_hWnd, 0, 0, PM_REMOVE)) {
            LOG_MSG(L"[LOG] DX11: Message received: ", msg.message);
            if (msg.message == WM_QUIT || msg.message == WM_CLOSE || msg.message == WM_KEYDOWN || msg.message == WM_KEYUP || msg.message == WM_SYSKEYDOWN || msg.message == WM_SYSKEYUP) {
                LOG_MSG(L"[LOG] DX11: Quit/close/key message received, exiting loop.");
                return nullptr;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Present();
        Sleep(16); // ~60 FPS
    }
    LOG_MSG(L"[LOG] DX11: Shutdown requested, exiting main loop.");
    return nullptr;
}
