// TileMap.cpp - fetches ESRI ArcGIS satellite + labels tiles via WinHTTP, composites
// them, and returns a cropped Gdiplus::Bitmap centered on a GPS coordinate.
// BLOCKING - must be called from a worker thread. See TileMap.h.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
using std::min;
using std::max;
#include <gdiplus.h>

#include <winhttp.h>
#include <objidl.h>
#include <shlobj.h>
#include <filesystem>
#include <vector>
#include <cmath>
#include <string>
#include <fstream>

#include "TileMap.h"
#include "Logger.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ole32.lib")

using namespace Gdiplus;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

// ---------------------------------------------------------------------------
// Web-Mercator helpers
// ---------------------------------------------------------------------------

// World pixel size at zoom z: W = 256 * 2^z
// lon -> pixel x
static double lon2x(double lon, double W) {
    return (lon + 180.0) / 360.0 * W;
}

// lat -> pixel y  (Mercator, top-left origin)
static double lat2y(double lat, double W) {
    double s = std::sin(lat * M_PI / 180.0);
    s = std::clamp(s, -0.9999, 0.9999);
    return (0.5 - std::log((1.0 + s) / (1.0 - s)) / (4.0 * M_PI)) * W;
}

// ---------------------------------------------------------------------------
// Disk tile cache helpers
// ---------------------------------------------------------------------------

static std::wstring GetCacheDir() {
    PWSTR appDataPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appDataPath))) {
        std::wstring dir = appDataPath;
        CoTaskMemFree(appDataPath);
        dir += L"\\HDRScreenSaver\\tilecache";
        return dir;
    }
    return L"";
}

// Read cached tile bytes into `out`. Returns true if cache hit.
static bool ReadCacheFile(const std::wstring& cacheDir, const std::wstring& cacheKey, std::vector<BYTE>& out) {
    if (cacheDir.empty()) return false;
    std::filesystem::path p = std::filesystem::path(cacheDir) / cacheKey;
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || std::filesystem::file_size(p, ec) == 0) return false;
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    if (sz <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(out.data()), sz);
    return f.good();
}

// Write tile bytes to cache (best-effort, never throws).
static void WriteCacheFile(const std::wstring& cacheDir, const std::wstring& cacheKey, const std::vector<BYTE>& data) {
    if (cacheDir.empty() || data.empty()) return;
    try {
        std::filesystem::path dir = cacheDir;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::filesystem::path p = dir / cacheKey;
        std::ofstream f(p, std::ios::binary);
        if (f) f.write(reinterpret_cast<const char*>(data.data()), data.size());
    } catch (...) {}
}

// ---------------------------------------------------------------------------
// WinHTTP per-tile fetch
// ---------------------------------------------------------------------------

// Fetches a tile from ArcGIS via WinHTTP. Checks disk cache first.
// path:      e.g. L"/ArcGIS/rest/services/World_Imagery/MapServer/tile/10/400/300"
// cacheKey:  e.g. L"sat_10_300_400.tile"
// Returns true and fills `out` on success; false on any error.
static bool FetchTile(HINTERNET connect, const std::wstring& path, const std::wstring& cacheKey,
                      const std::wstring& cacheDir, std::vector<BYTE>& out) {
    // Check disk cache first
    if (ReadCacheFile(cacheDir, cacheKey, out)) return true;

    // Open a new HTTPS request
    HINTERNET req = WinHttpOpenRequest(connect, L"GET", path.c_str(),
                                       nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE);
    if (!req) return false;

    // Send with User-Agent header
    const wchar_t* headers = L"User-Agent: HDRScreenSaver/1.0";
    if (!WinHttpSendRequest(req, headers, (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(req);
        return false;
    }
    if (!WinHttpReceiveResponse(req, nullptr)) {
        WinHttpCloseHandle(req);
        return false;
    }

    // Check HTTP status code
    DWORD statusCode = 0;
    DWORD scLen = sizeof(statusCode);
    if (!WinHttpQueryHeaders(req,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &statusCode, &scLen, WINHTTP_NO_HEADER_INDEX)) {
        WinHttpCloseHandle(req);
        return false;
    }
    if (statusCode != 200) {
        WinHttpCloseHandle(req);
        return false;
    }

    // Read body
    out.clear();
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(req, &available) && available > 0) {
        size_t offset = out.size();
        out.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(req, out.data() + offset, available, &read)) {
            WinHttpCloseHandle(req);
            return false;
        }
        if (read < available) out.resize(offset + read);
    }

    WinHttpCloseHandle(req);

    if (out.empty()) return false;

    // Write to disk cache (best-effort)
    WriteCacheFile(cacheDir, cacheKey, out);
    return true;
}

// ---------------------------------------------------------------------------
// Decode raw bytes into a Gdiplus::Bitmap via IStream
// ---------------------------------------------------------------------------
static Bitmap* DecodeBitmap(const std::vector<BYTE>& bytes) {
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return nullptr;

    ULONG written = 0;
    stream->Write(bytes.data(), static_cast<ULONG>(bytes.size()), &written);
    // Seek back to start
    LARGE_INTEGER li{}; li.QuadPart = 0;
    stream->Seek(li, STREAM_SEEK_SET, nullptr);

    Bitmap* bmp = Bitmap::FromStream(stream);
    stream->Release();
    if (!bmp || bmp->GetLastStatus() != Ok || bmp->GetWidth() == 0) {
        delete bmp;
        return nullptr;
    }
    return bmp;
}

} // namespace

// ---------------------------------------------------------------------------
// FetchSatelliteCrop - the public entry point
// ---------------------------------------------------------------------------

Gdiplus::Bitmap* FetchSatelliteCrop(double latitude, double longitude, double zoomDegrees) {
    if (zoomDegrees <= 0.0) zoomDegrees = 5.0;

    // -----------------------------------------------------------------------
    // 1. Choose zoom level z in [2..18] minimising |spanY - 768|
    // -----------------------------------------------------------------------
    int bestZ = 5;
    double bestDist = 1e18;
    for (int z = 2; z <= 18; ++z) {
        double W = 256.0 * static_cast<double>(1 << z);
        double spanY = lat2y(latitude - zoomDegrees, W) - lat2y(latitude + zoomDegrees, W);
        double dist = std::abs(spanY - 768.0);
        if (dist < bestDist) { bestDist = dist; bestZ = z; }
    }
    bestZ = std::clamp(bestZ, 2, 18);
    const int z = bestZ;
    const double W = 256.0 * static_cast<double>(1 << z);

    // -----------------------------------------------------------------------
    // 2. Pixel centre + crop side
    // -----------------------------------------------------------------------
    const double cx = lon2x(longitude, W);
    const double cy = lat2y(latitude, W);
    const double side = lat2y(latitude - zoomDegrees, W) - lat2y(latitude + zoomDegrees, W);
    if (side <= 0.0) return nullptr;

    const double left = cx - side / 2.0;
    const double top  = cy - side / 2.0;

    // -----------------------------------------------------------------------
    // 3. Tile ranges
    // -----------------------------------------------------------------------
    const int n = 1 << z;
    int tx0 = static_cast<int>(std::floor(left  / 256.0));
    int tx1 = static_cast<int>(std::floor((left + side) / 256.0));
    int ty0 = static_cast<int>(std::floor(top   / 256.0));
    int ty1 = static_cast<int>(std::floor((top  + side) / 256.0));
    ty0 = std::clamp(ty0, 0, n - 1);
    ty1 = std::clamp(ty1, 0, n - 1);

    const int canvasW = (tx1 - tx0 + 1) * 256;
    const int canvasH = (ty1 - ty0 + 1) * 256;

    // -----------------------------------------------------------------------
    // 4. Open WinHTTP session + connection (one per call)
    // -----------------------------------------------------------------------
    HINTERNET session = WinHttpOpen(L"HDRScreenSaver/1.0",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        LOG_MSG(L"TileMap: WinHttpOpen failed, falling back to offline map");
        return nullptr;
    }
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000);

    HINTERNET connect = WinHttpConnect(session, L"server.arcgisonline.com",
                                       INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        LOG_MSG(L"TileMap: WinHttpConnect failed, falling back to offline map");
        WinHttpCloseHandle(session);
        return nullptr;
    }

    const std::wstring cacheDir = GetCacheDir();

    // -----------------------------------------------------------------------
    // 5. Canvas
    // -----------------------------------------------------------------------
    Bitmap canvas(canvasW, canvasH, PixelFormat32bppARGB);
    if (canvas.GetLastStatus() != Ok) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return nullptr;
    }
    {
        Graphics cg(&canvas);
        cg.Clear(Color(0, 0, 0, 0));
        cg.SetInterpolationMode(InterpolationModeHighQualityBicubic);

        // -------------------------------------------------------------------
        // 6. Satellite tiles — any failure => return nullptr (offline fallback)
        // -------------------------------------------------------------------
        for (int ty = ty0; ty <= ty1; ++ty) {
            for (int tx = tx0; tx <= tx1; ++tx) {
                int txWrapped = ((tx % n) + n) % n;
                std::wstring path = L"/ArcGIS/rest/services/World_Imagery/MapServer/tile/" +
                                    std::to_wstring(z) + L"/" +
                                    std::to_wstring(ty) + L"/" +
                                    std::to_wstring(txWrapped);
                std::wstring cacheKey = L"sat_" + std::to_wstring(z) + L"_" +
                                        std::to_wstring(txWrapped) + L"_" +
                                        std::to_wstring(ty) + L".tile";

                std::vector<BYTE> bytes;
                if (!FetchTile(connect, path, cacheKey, cacheDir, bytes)) {
                    LOG_MSG(L"TileMap: satellite tile fetch failed z=" + std::to_wstring(z) +
                            L" tx=" + std::to_wstring(txWrapped) +
                            L" ty=" + std::to_wstring(ty) + L", falling back to offline map");
                    WinHttpCloseHandle(connect);
                    WinHttpCloseHandle(session);
                    return nullptr;
                }
                Bitmap* tile = DecodeBitmap(bytes);
                if (!tile) {
                    LOG_MSG(L"TileMap: satellite tile decode failed, falling back to offline map");
                    WinHttpCloseHandle(connect);
                    WinHttpCloseHandle(session);
                    return nullptr;
                }
                cg.DrawImage(tile,
                             (tx - tx0) * 256, (ty - ty0) * 256,
                             256, 256);
                delete tile;
            }
        }

        // -------------------------------------------------------------------
        // 7. Label overlay tiles — best-effort (skip on failure)
        // -------------------------------------------------------------------
        for (int ty = ty0; ty <= ty1; ++ty) {
            for (int tx = tx0; tx <= tx1; ++tx) {
                int txWrapped = ((tx % n) + n) % n;
                std::wstring path = L"/ArcGIS/rest/services/Reference/World_Boundaries_and_Places/MapServer/tile/" +
                                    std::to_wstring(z) + L"/" +
                                    std::to_wstring(ty) + L"/" +
                                    std::to_wstring(txWrapped);
                std::wstring cacheKey = L"lbl_" + std::to_wstring(z) + L"_" +
                                        std::to_wstring(txWrapped) + L"_" +
                                        std::to_wstring(ty) + L".tile";

                std::vector<BYTE> bytes;
                if (!FetchTile(connect, path, cacheKey, cacheDir, bytes)) continue; // skip on failure
                Bitmap* tile = DecodeBitmap(bytes);
                if (!tile) continue;
                cg.DrawImage(tile,
                             (tx - tx0) * 256, (ty - ty0) * 256,
                             256, 256);
                delete tile;
            }
        }
    }

    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    // -----------------------------------------------------------------------
    // 8. Crop canvas to the exact requested square
    // -----------------------------------------------------------------------
    const INT cropW = static_cast<INT>(std::round(side));
    const INT cropH = static_cast<INT>(std::round(side));
    if (cropW <= 0 || cropH <= 0) return nullptr;

    Bitmap* result = new Bitmap(cropW, cropH, PixelFormat32bppARGB);
    if (!result || result->GetLastStatus() != Ok) {
        delete result;
        return nullptr;
    }
    {
        Graphics rg(result);
        rg.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        rg.Clear(Color(255, 0, 0, 0));

        // Source rect within the canvas
        REAL srcX = static_cast<REAL>(left - tx0 * 256.0);
        REAL srcY = static_cast<REAL>(top  - ty0 * 256.0);
        REAL srcS = static_cast<REAL>(side);

        rg.DrawImage(&canvas,
                     RectF(0.0f, 0.0f, static_cast<REAL>(cropW), static_cast<REAL>(cropH)),
                     srcX, srcY, srcS, srcS,
                     UnitPixel);
    }
    return result;
}
