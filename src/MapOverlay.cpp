// MapOverlay.cpp - layered-window map overlay. See MapOverlay.h.
// Supports both offline (bundled equirectangular world_map.jpg) and online (ESRI
// ArcGIS satellite tiles fetched on a worker thread via WinHTTP).

#define NOMINMAX
#include <windows.h>
#include <algorithm>
// GDI+ headers reference unqualified min/max; with NOMINMAX (so std::clamp/min/max work)
// we bring the std versions into the global namespace before including gdiplus.h.
using std::min;
using std::max;
#include <gdiplus.h>
#include <filesystem>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "MapOverlay.h"
#include "TileMap.h"
#include "Logger.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using namespace Gdiplus;

// Posted by the worker thread when a new satellite crop is ready.
// lParam = Gdiplus::Bitmap*   wParam = request generation number
// Safe to use WM_APP+1 on the overlay window — the host window's hook messages
// (also WM_APP+1) are posted to the host HWND, a different window.
#define WM_APP_TILES_READY (WM_APP + 1)

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

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK MapOverlay::OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    MapOverlay* self = reinterpret_cast<MapOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_APP_TILES_READY) {
        Bitmap* bmp = reinterpret_cast<Bitmap*>(lParam);
        unsigned gen = static_cast<unsigned>(wParam);
        if (self && gen == self->m_gen.load()) {
            // Adopt the new bitmap
            if (self->m_onlineBitmap) {
                delete static_cast<Bitmap*>(self->m_onlineBitmap);
            }
            self->m_onlineBitmap = bmp;
            self->m_hasOnline = true;
            if (self->m_wantVisible && !self->m_suspended) {
                self->Redraw();
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
        } else {
            // Stale result or no self — discard
            delete bmp;
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------

bool MapOverlay::Initialize(HWND owner, double zoomDegrees, bool onlineMode)
{
    m_owner = owner;
    m_zoomDegrees = (zoomDegrees > 0.0 && zoomDegrees <= 90.0) ? zoomDegrees : 10.0;
    m_onlineMode = onlineMode;

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &gdiplusStartupInput, nullptr) != Ok) {
        LOG_MSG(L"MapOverlay: GdiplusStartup failed");
        return false;
    }
    m_gdiplusToken = token;

    WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kOverlayClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        LOG_MSG(L"MapOverlay: RegisterClassExW failed");
        return false;
    }

    // Layered, click-through, no-activate popup owned by the host window.
    // Pass `this` as lpParam so OverlayWndProc can capture it via WM_NCCREATE.
    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        kOverlayClassName, L"", WS_POPUP,
        0, 0, 10, 10,
        owner, nullptr, GetModuleHandleW(nullptr), this);
    if (!m_hwnd) {
        LOG_MSG(L"MapOverlay: CreateWindowExW failed");
        return false;
    }

    m_stop = false;
    m_worker = std::thread(&MapOverlay::WorkerLoop, this);

    return true;
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------

void MapOverlay::Shutdown()
{
    // Stop the worker thread first
    if (m_worker.joinable()) {
        {
            std::lock_guard<std::mutex> lk(m_mx);
            m_stop = true;
        }
        m_cv.notify_one();
        m_worker.join();
    }

    Hide();

    if (m_onlineBitmap) {
        delete static_cast<Bitmap*>(m_onlineBitmap);
        m_onlineBitmap = nullptr;
    }
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

// ---------------------------------------------------------------------------
// EnsureWorldLoaded
// ---------------------------------------------------------------------------

bool MapOverlay::EnsureWorldLoaded()
{
    if (m_worldBitmap) return true;
    if (m_loadAttempted) return false;
    m_loadAttempted = true;

    const std::wstring path = FindWorldMapPath();
    if (path.empty()) {
        LOG_MSG(L"MapOverlay: world map image not found (expected world_map.jpg next to the executable). Offline map disabled.");
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

// ---------------------------------------------------------------------------
// Show
// ---------------------------------------------------------------------------

void MapOverlay::Show(const std::wstring& dateText, const std::wstring& locationText,
                      bool hasGps, double latitude, double longitude)
{
    m_date = dateText;
    m_location = locationText;
    m_lat = latitude;
    m_lon = longitude;

    // Reserve space for a map if we have GPS and either the offline asset is available
    // or online mode is on (tile fetch pending)
    bool worldOk = EnsureWorldLoaded();
    m_hasMap = hasGps && (worldOk || m_onlineMode);

    if (!m_hasMap && m_date.empty() && m_location.empty()) {  // nothing to show
        Hide();
        return;
    }

    // Bump generation and discard any previous online result
    unsigned g = ++m_gen;
    m_hasOnline = false;
    if (m_onlineBitmap) {
        delete static_cast<Bitmap*>(m_onlineBitmap);
        m_onlineBitmap = nullptr;
    }

    // Queue a tile fetch if in online mode and we have GPS
    if (m_onlineMode && hasGps) {
        {
            std::lock_guard<std::mutex> lk(m_mx);
            m_req = { latitude, longitude, m_zoomDegrees, g, true };
        }
        m_cv.notify_one();
    }

    m_wantVisible = true;
    if (m_suspended) return;  // owner not active; will show on resume
    Redraw();
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

// ---------------------------------------------------------------------------
// Hide
// ---------------------------------------------------------------------------

void MapOverlay::Hide()
{
    m_wantVisible = false;
    if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE);
}

// ---------------------------------------------------------------------------
// SetOnlineMode
// ---------------------------------------------------------------------------

void MapOverlay::SetOnlineMode(bool online)
{
    m_onlineMode = online;
}

// ---------------------------------------------------------------------------
// Reposition
// ---------------------------------------------------------------------------

void MapOverlay::Reposition()
{
    if (m_wantVisible && !m_suspended) {
        Redraw();
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

// ---------------------------------------------------------------------------
// SetOwnerActive
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Redraw  — the core painting function
// ---------------------------------------------------------------------------

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
    const REAL fontPx = std::max(10.0f, side * 0.085f);
    const int bandH = static_cast<int>(fontPx * 1.6f);  // shared height for both text rows
    const int locBand  = m_location.empty() ? 0 : bandH;
    const int textBand = m_date.empty()     ? 0 : bandH;
    if (mapH == 0 && locBand == 0 && textBand == 0) return;
    const int winW = side;
    const int winH = mapH + locBand + textBand;
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
            // ------------------------------------------------------------------
            // Helper lambdas for frame and pin (used by all three branches)
            // ------------------------------------------------------------------
            auto drawFrame = [&]() {
                Pen outer(Color(160, 0, 0, 0), 1.0f);
                g.DrawRectangle(&outer, 0, 0, side - 1, side - 1);
                Pen inner(Color(210, 255, 255, 255), 2.0f);
                g.DrawRectangle(&inner, 1, 1, side - 3, side - 3);
            };

            auto drawPin = [&](REAL pinX, REAL pinY) {
                const REAL r = std::max(5.0f, side * 0.018f);
                SolidBrush dot(Color(255, 225, 45, 45));
                Pen ring(Color(255, 255, 255, 255), 2.0f);
                g.FillEllipse(&dot, pinX - r, pinY - r, 2 * r, 2 * r);
                g.DrawEllipse(&ring, pinX - r, pinY - r, 2 * r, 2 * r);
                SolidBrush center(Color(255, 255, 255, 255));
                g.FillEllipse(&center, pinX - r * 0.35f, pinY - r * 0.35f, r * 0.7f, r * 0.7f);
            };

            if (m_onlineMode && m_hasOnline && m_onlineBitmap) {
                // -------------------------------------------------------
                // Branch A: online satellite bitmap is ready — draw it
                // -------------------------------------------------------
                Bitmap* sat = static_cast<Bitmap*>(m_onlineBitmap);
                g.DrawImage(sat,
                            RectF(0.0f, 0.0f, static_cast<REAL>(side), static_cast<REAL>(side)),
                            0.0f, 0.0f,
                            static_cast<REAL>(sat->GetWidth()),
                            static_cast<REAL>(sat->GetHeight()),
                            UnitPixel);
                drawFrame();
                // Pin at centre (the satellite crop is already centred on the location)
                drawPin(static_cast<REAL>(side) / 2.0f, static_cast<REAL>(side) / 2.0f);

            } else if (m_worldBitmap) {
                // -------------------------------------------------------
                // Branch B: offline world map (same as original code)
                // -------------------------------------------------------
                auto* world = static_cast<Bitmap*>(m_worldBitmap);
                const double worldW = static_cast<double>(world->GetWidth());
                const double worldH = static_cast<double>(world->GetHeight());

                const double centerX = (m_lon + 180.0) / 360.0 * worldW;
                const double centerY = (90.0 - m_lat) / 180.0 * worldH;
                double cropSize = (m_zoomDegrees / 90.0) * worldH;
                cropSize = std::clamp(cropSize, static_cast<double>(side), worldH);
                double srcLeft = centerX - cropSize / 2.0;
                double srcTop  = centerY - cropSize / 2.0;
                srcLeft = std::clamp(srcLeft, 0.0, std::max(0.0, worldW - cropSize));
                srcTop  = std::clamp(srcTop,  0.0, std::max(0.0, worldH - cropSize));

                g.DrawImage(world,
                            RectF(0.0f, 0.0f, static_cast<REAL>(side), static_cast<REAL>(side)),
                            static_cast<REAL>(srcLeft), static_cast<REAL>(srcTop),
                            static_cast<REAL>(cropSize), static_cast<REAL>(cropSize),
                            UnitPixel);
                drawFrame();
                // Pin at true projected location within the (possibly shifted) crop
                const REAL pinX = static_cast<REAL>((centerX - srcLeft) / cropSize * side);
                const REAL pinY = static_cast<REAL>((centerY - srcTop)  / cropSize * side);
                drawPin(pinX, pinY);

            } else {
                // -------------------------------------------------------
                // Branch C: online pending, no offline asset — dark placeholder
                // -------------------------------------------------------
                SolidBrush bg(Color(200, 30, 30, 35));
                g.FillRectangle(&bg, 0, 0, side, side);
                drawFrame();
                drawPin(static_cast<REAL>(side) / 2.0f, static_cast<REAL>(side) / 2.0f);
            }
        }

        // Text bands below the map: location row then date row.
        if (locBand > 0 || textBand > 0) {
            FontFamily ff(L"Segoe UI");
            Font font(&ff, fontPx, FontStyleRegular, UnitPixel);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            sf.SetTrimming(StringTrimmingEllipsisCharacter);
            sf.SetFormatFlags(StringFormatFlagsNoWrap);
            g.SetTextRenderingHint(TextRenderingHintAntiAlias);

            SolidBrush shadow(Color(140, 0, 0, 0));
            SolidBrush textBrush(Color(191, 255, 255, 255));

            auto drawRow = [&](const std::wstring& str, int yTop, int rowH) {
                const RectF rShadow(1.0f, static_cast<REAL>(yTop) + 1.0f, static_cast<REAL>(winW), static_cast<REAL>(rowH));
                const RectF rText (0.0f, static_cast<REAL>(yTop),        static_cast<REAL>(winW), static_cast<REAL>(rowH));
                g.DrawString(str.c_str(), -1, &font, rShadow, &sf, &shadow);
                g.DrawString(str.c_str(), -1, &font, rText,   &sf, &textBrush);
            };

            if (locBand > 0)  drawRow(m_location, mapH,            locBand);
            if (textBand > 0) drawRow(m_date,      mapH + locBand, textBand);
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

// ---------------------------------------------------------------------------
// WorkerLoop  — runs on a background thread; fetches satellite tiles
// ---------------------------------------------------------------------------

void MapOverlay::WorkerLoop()
{
    for (;;) {
        Req req;
        {
            std::unique_lock<std::mutex> lk(m_mx);
            m_cv.wait(lk, [this] { return m_stop || m_req.valid; });
            if (m_stop) return;
            req = m_req;
            m_req.valid = false;
        }

        Bitmap* bmp = FetchSatelliteCrop(req.lat, req.lon, req.zoom);
        if (bmp) {
            if (req.gen == m_gen.load() && m_hwnd) {
                // Post to the overlay's own message queue — dispatched on the UI thread
                PostMessageW(m_hwnd, WM_APP_TILES_READY,
                             static_cast<WPARAM>(req.gen),
                             reinterpret_cast<LPARAM>(bmp));
            } else {
                delete bmp;  // stale or shutting down
            }
        }
        // If bmp==nullptr the fetch failed; the overlay keeps showing whatever it had
        // (offline map or placeholder).
    }
}
