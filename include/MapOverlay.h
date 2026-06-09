#pragma once

// MapOverlay.h - a small, non-interactive map shown in the lower-left corner of the
// host window. Renders a pin at a given GPS location by cropping a bundled offline
// equirectangular world map. Implemented as a layered, click-through popup window so
// it floats above the WebView2 control without touching the image-rendering path.

#include <windows.h>
#include <string>

class MapOverlay {
public:
    // Creates the layered window owned by `owner`. The world map image is loaded lazily
    // on first ShowAt(). `zoomDegrees` is the half-extent (in degrees) cropped around the
    // pin, controlling effective zoom. Returns true if the window was created.
    bool Initialize(HWND owner, int zoomDegrees);

    // Destroys the window and frees the world map bitmap.
    void Shutdown();

    // True once the bundled world map image has been found and loaded.
    bool IsMapLoaded() const { return m_worldBitmap != nullptr; }

    // Shows the overlay for the current photo. The date label (if any) is anchored at the
    // lower-left corner; when hasGps is true a map cropped around the coordinate is stacked
    // above it. The world map is loaded on first use; if its asset is missing only the date
    // is shown. No-op if there is neither a date nor a usable map.
    void Show(const std::wstring& dateText, bool hasGps, double latitude, double longitude);

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

    HWND       m_owner = nullptr;
    HWND       m_hwnd = nullptr;
    void*      m_worldBitmap = nullptr;  // Gdiplus::Bitmap*
    unsigned long long m_gdiplusToken = 0;
    int        m_zoomDegrees = 10;
    bool       m_wantVisible = false;  // logically should be shown for the current image
    bool       m_suspended = false;    // owner window is not active (alt-tabbed away)
    bool       m_loadAttempted = false;
    bool       m_hasMap = false;  // current photo has GPS and the world map is loaded
    double     m_lat = 0.0;
    double     m_lon = 0.0;
    std::wstring m_date;  // formatted photo date drawn at the lower-left (empty = none)
};
