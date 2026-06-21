#pragma once

// MapOverlay.h - a small, non-interactive map shown in the lower-left corner of the
// host window. In offline mode it renders a pin on a bundled equirectangular world map;
// in online mode it fetches high-quality ESRI ArcGIS satellite tiles via WinHTTP on a
// worker thread and composites them. Implemented as a layered, click-through popup
// window so it floats above the WebView2 control without touching the image-rendering path.

#include <windows.h>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

class MapOverlay {
public:
    // Creates the layered window owned by `owner`. The world map image is loaded lazily
    // on first Show(). `zoomDegrees` is the half-extent (in degrees) cropped around the
    // pin, controlling effective zoom. `onlineMode` enables the satellite tile fetch path.
    // Returns true if the window was created.
    bool Initialize(HWND owner, double zoomDegrees, bool onlineMode);

    // Destroys the window and frees bitmaps. Joins the worker thread if running.
    void Shutdown();

    // True once the bundled world map image has been found and loaded.
    bool IsMapLoaded() const { return m_worldBitmap != nullptr; }

    // Shows the overlay for the current photo. The date label (if any) is anchored at the
    // lower-left corner; when hasGps is true a map cropped around the coordinate is stacked
    // above it. The world map is loaded on first use; if its asset is missing only the date
    // is shown. No-op if there is neither a date nor a usable map.
    void Show(const std::wstring& dateText, bool hasGps, double latitude, double longitude);

    // Switches between online (satellite) and offline (world map) rendering at runtime.
    // Safe to call before the next Show().
    void SetOnlineMode(bool online);

    // Hides the overlay window.
    void Hide();

    // Recomputes position/size relative to the owner window and re-asserts z-order.
    // Call after the owner window is resized/moved.
    void Reposition();

    // Suspends/resumes the overlay when the owner window loses/gains activation, so it
    // does not float above other apps after the user alt-tabs away.
    void SetOwnerActive(bool active);

private:
    bool EnsureWorldLoaded();
    void Redraw();
    void WorkerLoop();
    static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND       m_owner = nullptr;
    HWND       m_hwnd = nullptr;
    void*      m_worldBitmap = nullptr;   // Gdiplus::Bitmap* offline world map (owned)
    unsigned long long m_gdiplusToken = 0;
    double     m_zoomDegrees = 10.0;
    bool       m_wantVisible = false;     // logically should be shown for the current image
    bool       m_suspended = false;       // owner window is not active (alt-tabbed away)
    bool       m_loadAttempted = false;
    bool       m_hasMap = false;          // current photo has GPS (and offline or online map available)
    double     m_lat = 0.0;
    double     m_lon = 0.0;
    std::wstring m_date;                  // formatted photo date drawn at the lower-left (empty = none)

    // Online satellite tile path
    bool       m_onlineMode = false;
    void*      m_onlineBitmap = nullptr;  // Gdiplus::Bitmap* satellite crop for current image (owned)
    bool       m_hasOnline = false;
    std::atomic<unsigned> m_gen{0};       // bumped each Show(); identifies the current request

    // Worker thread state
    std::thread              m_worker;
    std::mutex               m_mx;
    std::condition_variable  m_cv;
    bool                     m_stop = false;
    struct Req {
        double lat = 0;
        double lon = 0;
        double zoom = 0;
        unsigned gen = 0;
        bool   valid = false;
    } m_req;
};
