// MapOverlay.cpp - layered-window offline map overlay. See MapOverlay.h.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
// GDI+ headers reference unqualified min/max; with NOMINMAX (so std::clamp/min/max work)
// we bring the std versions into the global namespace before including gdiplus.h.
using std::min;
using std::max;
#include <gdiplus.h>
#include <filesystem>

#include "MapOverlay.h"
#include "Logger.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using namespace Gdiplus;

namespace {

const wchar_t* kOverlayClassName = L"HDRScreenSaverMapOverlay";

// Locate the bundled world map image relative to the running executable.
std::wstring FindWorldMapPath()
{
    wchar_t exePath[MAX_PATH] = {0};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return L"";
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();

    const std::filesystem::path candidates[] = {
        exeDir / L"world_map.jpg",
        exeDir / L"resources" / L"world_map.jpg",
        exeDir / L".." / L".." / L"resources" / L"world_map.jpg",       // dev: build/Release
        exeDir / L".." / L".." / L".." / L"resources" / L"world_map.jpg",
    };
    for (const auto& c : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(c, ec) && std::filesystem::is_regular_file(c, ec)) {
            return std::filesystem::weakly_canonical(c, ec).wstring();
        }
    }
    return L"";
}

} // namespace

bool MapOverlay::Initialize(HWND owner, int zoomDegrees)
{
    m_owner = owner;
    m_zoomDegrees = (zoomDegrees > 0 && zoomDegrees <= 90) ? zoomDegrees : 10;

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &gdiplusStartupInput, nullptr) != Ok) {
        LOG_MSG(L"MapOverlay: GdiplusStartup failed");
        return false;
    }
    m_gdiplusToken = token;

    WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kOverlayClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        LOG_MSG(L"MapOverlay: RegisterClassExW failed");
        return false;
    }

    // Layered, click-through, no-activate popup owned by the host window.
    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        kOverlayClassName, L"", WS_POPUP,
        0, 0, 10, 10,
        owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!m_hwnd) {
        LOG_MSG(L"MapOverlay: CreateWindowExW failed");
        return false;
    }
    return true;
}

void MapOverlay::Shutdown()
{
    Hide();
    if (m_worldBitmap) {
        delete static_cast<Bitmap*>(m_worldBitmap);
        m_worldBitmap = nullptr;
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_gdiplusToken) {
        GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }
}

bool MapOverlay::EnsureWorldLoaded()
{
    if (m_worldBitmap) return true;
    if (m_loadAttempted) return false;
    m_loadAttempted = true;

    const std::wstring path = FindWorldMapPath();
    if (path.empty()) {
        LOG_MSG(L"MapOverlay: world map image not found (expected world_map.jpg next to the executable). Map overlay disabled.");
        return false;
    }

    auto* bmp = new Bitmap(path.c_str());
    if (bmp->GetLastStatus() != Ok || bmp->GetWidth() == 0 || bmp->GetHeight() == 0) {
        LOG_MSG(L"MapOverlay: failed to load world map image: " + path);
        delete bmp;
        return false;
    }
    m_worldBitmap = bmp;
    LOG_MSG(L"MapOverlay: loaded world map " + path + L" (" +
            std::to_wstring(bmp->GetWidth()) + L"x" + std::to_wstring(bmp->GetHeight()) + L")");
    return true;
}

void MapOverlay::Show(const std::wstring& dateText, bool hasGps, double latitude, double longitude)
{
    m_date = dateText;
    m_lat = latitude;
    m_lon = longitude;
    m_hasMap = hasGps && EnsureWorldLoaded();
    if (!m_hasMap && m_date.empty()) {  // nothing to show
        Hide();
        return;
    }
    m_wantVisible = true;
    if (m_suspended) return;  // owner not active; will show on resume
    Redraw();
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void MapOverlay::Hide()
{
    m_wantVisible = false;
    if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE);
}

void MapOverlay::Reposition()
{
    if (m_wantVisible && !m_suspended) {
        Redraw();
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void MapOverlay::SetOwnerActive(bool active)
{
    m_suspended = !active;
    if (!m_hwnd) return;
    if (active) {
        if (m_wantVisible) {
            Redraw();
            SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    } else {
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

void MapOverlay::Redraw()
{
    if (!m_hwnd) return;

    // Position/size relative to the owner window's lower-left corner.
    RECT orc{};
    if (!GetWindowRect(m_owner, &orc)) return;
    const int ownerW = orc.right - orc.left;
    const int ownerH = orc.bottom - orc.top;
    if (ownerW <= 0 || ownerH <= 0) return;

    int side = static_cast<int>(ownerW * 0.10);
    side = std::clamp(side, 120, static_cast<int>(ownerH * 0.40));
    const int margin = std::max(20, side / 12);
    const int mapH = m_hasMap ? side : 0;
    // Date band sits below the map, anchored at the lower-left corner.
    const int textBand = m_date.empty() ? 0 : std::max(16, static_cast<int>(side * 0.16));
    if (mapH == 0 && textBand == 0) return;
    const int winW = side;
    const int winH = mapH + textBand;
    const int left = orc.left + margin;
    const int top = orc.bottom - margin - winH;

    // Build a 32bpp premultiplied top-down DIB to feed UpdateLayeredWindow.
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = winW;
    bmi.bmiHeader.biHeight = -winH; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screenDC = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib) {
        ReleaseDC(nullptr, screenDC);
        return;
    }

    {
        Bitmap target(winW, winH, winW * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Graphics g(&target);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.Clear(Color(0, 0, 0, 0));

        if (m_hasMap) {
            auto* world = static_cast<Bitmap*>(m_worldBitmap);
            const double worldW = static_cast<double>(world->GetWidth());
            const double worldH = static_cast<double>(world->GetHeight());

            // Crop a square window of +/- m_zoomDegrees around the pin. Because an equirectangular
            // map is 2:1, equal degree extents in lat and lon map to an equal number of pixels,
            // giving a consistent real-world area regardless of the on-screen box size.
            const double centerX = (m_lon + 180.0) / 360.0 * worldW;
            const double centerY = (90.0 - m_lat) / 180.0 * worldH;
            double cropSize = (static_cast<double>(m_zoomDegrees) / 90.0) * worldH; // full crop side in px
            cropSize = std::clamp(cropSize, 8.0, worldH);
            double srcLeft = centerX - cropSize / 2.0;
            double srcTop = centerY - cropSize / 2.0;
            // Keep the crop inside the image; the pin is drawn at its true projected position.
            srcLeft = std::clamp(srcLeft, 0.0, std::max(0.0, worldW - cropSize));
            srcTop = std::clamp(srcTop, 0.0, std::max(0.0, worldH - cropSize));

            g.DrawImage(world,
                        RectF(0.0f, 0.0f, static_cast<REAL>(side), static_cast<REAL>(side)),
                        static_cast<REAL>(srcLeft), static_cast<REAL>(srcTop),
                        static_cast<REAL>(cropSize), static_cast<REAL>(cropSize),
                        UnitPixel);

            // Frame.
            Pen outer(Color(160, 0, 0, 0), 1.0f);
            g.DrawRectangle(&outer, 0, 0, side - 1, side - 1);
            Pen inner(Color(210, 255, 255, 255), 2.0f);
            g.DrawRectangle(&inner, 1, 1, side - 3, side - 3);

            // Pin at the true projected location within the (possibly shifted) crop.
            const REAL pinX = static_cast<REAL>((centerX - srcLeft) / cropSize * side);
            const REAL pinY = static_cast<REAL>((centerY - srcTop) / cropSize * side);
            const REAL r = std::max(5.0f, side * 0.018f);
            SolidBrush dot(Color(255, 225, 45, 45));
            Pen ring(Color(255, 255, 255, 255), 2.0f);
            g.FillEllipse(&dot, pinX - r, pinY - r, 2 * r, 2 * r);
            g.DrawEllipse(&ring, pinX - r, pinY - r, 2 * r, 2 * r);
            SolidBrush center(Color(255, 255, 255, 255));
            g.FillEllipse(&center, pinX - r * 0.35f, pinY - r * 0.35f, r * 0.7f, r * 0.7f);
        }

        // Photo date in the band below the map (~75% opaque white, soft shadow for legibility).
        if (textBand > 0) {
            FontFamily ff(L"Segoe UI");
            const REAL fontPx = std::max(10.0f, side * 0.085f);
            Font font(&ff, fontPx, FontStyleRegular, UnitPixel);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            g.SetTextRenderingHint(TextRenderingHintAntiAlias);
            const REAL bandTop = static_cast<REAL>(mapH);
            SolidBrush shadow(Color(140, 0, 0, 0));
            g.DrawString(m_date.c_str(), -1, &font,
                         RectF(1.0f, bandTop + 1.0f, static_cast<REAL>(winW), static_cast<REAL>(textBand)), &sf, &shadow);
            SolidBrush text(Color(191, 255, 255, 255)); // ~75% opaque
            g.DrawString(m_date.c_str(), -1, &font,
                         RectF(0.0f, bandTop, static_cast<REAL>(winW), static_cast<REAL>(textBand)), &sf, &text);
        }
        g.Flush();
    }

    HDC memDC = CreateCompatibleDC(screenDC);
    HGDIOBJ oldBmp = SelectObject(memDC, dib);

    POINT ptDst{ left, top };
    SIZE sz{ winW, winH };
    POINT ptSrc{ 0, 0 };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(m_hwnd, screenDC, &ptDst, &sz, memDC, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    DeleteObject(dib);
    ReleaseDC(nullptr, screenDC);
}
