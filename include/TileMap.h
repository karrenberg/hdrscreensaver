#pragma once

namespace Gdiplus { class Bitmap; }

// Fetches keyless ESRI satellite + labels tiles around (lat,lon), composites a
// centered square covering ~(2*zoomDegrees) degrees of latitude, and returns a
// newly-allocated Gdiplus::Bitmap (caller owns via delete). Returns nullptr on any
// network/decode failure (caller then keeps the offline map). BLOCKING — call from a
// worker thread. Uses a disk tile cache under %LOCALAPPDATA%\HDRScreenSaver\tilecache.
Gdiplus::Bitmap* FetchSatelliteCrop(double latitude, double longitude, double zoomDegrees);
