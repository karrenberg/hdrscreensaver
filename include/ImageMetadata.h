#pragma once

// ImageMetadata.h - header-only helpers to read EXIF GPS coordinates from an image
// using the Windows Imaging Component (WIC). COM is initialized by the host before use.

#include <windows.h>
#include <wincodec.h>
#include <propvarutil.h>
#include <wrl.h>
#include <string>
#include <cwctype>
#include <cstdio>

#include "Logger.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "ole32.lib")

struct GpsLocation {
    bool   hasGps = false;
    double latitude = 0.0;   // decimal degrees, +N / -S
    double longitude = 0.0;  // decimal degrees, +E / -W
};

namespace ImageMetadataDetail {

// Convert one packed WIC EXIF rational (LODWORD = numerator, HIDWORD = denominator) to double.
static inline double RationalToDouble(ULONGLONG packed)
{
    const DWORD numerator = static_cast<DWORD>(packed & 0xFFFFFFFFull);
    const DWORD denominator = static_cast<DWORD>(packed >> 32);
    if (denominator == 0) return 0.0;
    return static_cast<double>(numerator) / static_cast<double>(denominator);
}

// Read a 3-element (deg, min, sec) GPS coordinate value and convert to decimal degrees.
// Returns false if the value is missing or not in the expected form.
static inline bool ReadCoordinate(IWICMetadataQueryReader* reader, const wchar_t* query, double& outDegrees)
{
    PROPVARIANT v;
    PropVariantInit(&v);
    bool ok = false;
    if (SUCCEEDED(reader->GetMetadataByName(query, &v))) {
        if (v.vt == (VT_VECTOR | VT_UI8) && v.cauh.cElems >= 3) {
            const double deg = RationalToDouble(v.cauh.pElems[0].QuadPart);
            const double min = RationalToDouble(v.cauh.pElems[1].QuadPart);
            const double sec = RationalToDouble(v.cauh.pElems[2].QuadPart);
            outDegrees = deg + min / 60.0 + sec / 3600.0;
            ok = true;
        }
    }
    PropVariantClear(&v);
    return ok;
}

// Read a GPS reference char ('N'/'S'/'E'/'W'). Returns 0 if missing.
static inline wchar_t ReadRef(IWICMetadataQueryReader* reader, const wchar_t* query)
{
    PROPVARIANT v;
    PropVariantInit(&v);
    wchar_t result = 0;
    if (SUCCEEDED(reader->GetMetadataByName(query, &v))) {
        if (v.vt == VT_LPSTR && v.pszVal && v.pszVal[0]) {
            result = static_cast<wchar_t>(v.pszVal[0]);
        } else if (v.vt == VT_LPWSTR && v.pwszVal && v.pwszVal[0]) {
            result = v.pwszVal[0];
        }
    }
    PropVariantClear(&v);
    return towupper(result);
}

} // namespace ImageMetadataDetail

// Reads EXIF GPS coordinates from the given image file. Returns a GpsLocation with
// hasGps == false if the image has no usable GPS metadata or cannot be read.
static inline GpsLocation ReadGpsLocation(const std::wstring& imagePath)
{
    using namespace Microsoft::WRL;
    using namespace ImageMetadataDetail;

    GpsLocation loc;

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))) || !factory) {
        return loc;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(imagePath.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, &decoder)) || !decoder) {
        return loc;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)) || !frame) {
        return loc;
    }

    ComPtr<IWICMetadataQueryReader> reader;
    if (FAILED(frame->GetMetadataQueryReader(&reader)) || !reader) {
        return loc;
    }

    // JPEG stores GPS under /app1/ifd/gps; TIFF under /ifd/gps. Try both.
    static const wchar_t* kLatRefPaths[]  = { L"/app1/ifd/gps/{ushort=1}", L"/ifd/gps/{ushort=1}" };
    static const wchar_t* kLatPaths[]     = { L"/app1/ifd/gps/{ushort=2}", L"/ifd/gps/{ushort=2}" };
    static const wchar_t* kLonRefPaths[]  = { L"/app1/ifd/gps/{ushort=3}", L"/ifd/gps/{ushort=3}" };
    static const wchar_t* kLonPaths[]     = { L"/app1/ifd/gps/{ushort=4}", L"/ifd/gps/{ushort=4}" };

    for (int i = 0; i < 2; ++i) {
        double lat = 0.0, lon = 0.0;
        if (ReadCoordinate(reader.Get(), kLatPaths[i], lat) &&
            ReadCoordinate(reader.Get(), kLonPaths[i], lon)) {
            const wchar_t latRef = ReadRef(reader.Get(), kLatRefPaths[i]);
            const wchar_t lonRef = ReadRef(reader.Get(), kLonRefPaths[i]);
            if (latRef == L'S') lat = -lat;
            if (lonRef == L'W') lon = -lon;

            // Reject obviously invalid / zero-island coordinates.
            if (lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0 &&
                !(lat == 0.0 && lon == 0.0)) {
                loc.hasGps = true;
                loc.latitude = lat;
                loc.longitude = lon;
                LOG_MSG(L"ReadGpsLocation: " + imagePath + L" -> lat=" + std::to_wstring(lat) + L", lon=" + std::to_wstring(lon));
            }
            break;
        }
    }

    if (!loc.hasGps) {
        LOG_MSG(L"ReadGpsLocation: no GPS metadata for " + imagePath);
    }
    return loc;
}

namespace ImageMetadataDetail {

// Format an EXIF date string "YYYY:MM:DD HH:MM:SS" as e.g. "5 June 2018". Returns empty on failure.
static inline std::wstring FormatExifDate(const char* exif)
{
    if (!exif) return L"";
    int y = 0, mo = 0, d = 0;
    if (sscanf_s(exif, "%4d:%2d:%2d", &y, &mo, &d) != 3) return L"";
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || y < 1) return L"";
    static const wchar_t* kMonths[] = { L"January", L"February", L"March", L"April", L"May", L"June",
                                        L"July", L"August", L"September", L"October", L"November", L"December" };
    return std::to_wstring(d) + L" " + kMonths[mo - 1] + L" " + std::to_wstring(y);
}

} // namespace ImageMetadataDetail

// Reads the EXIF capture date ("date taken") and returns it formatted (e.g. "5 June 2018").
// Returns an empty string if the image has no usable date metadata.
static inline std::wstring ReadDateTaken(const std::wstring& imagePath)
{
    using namespace Microsoft::WRL;
    using namespace ImageMetadataDetail;

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))) || !factory) {
        return L"";
    }
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(imagePath.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, &decoder)) || !decoder) {
        return L"";
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)) || !frame) return L"";
    ComPtr<IWICMetadataQueryReader> reader;
    if (FAILED(frame->GetMetadataQueryReader(&reader)) || !reader) return L"";

    // DateTimeOriginal (0x9003) lives in the Exif sub-IFD; fall back to DateTime (0x0132).
    static const wchar_t* kPaths[] = {
        L"/app1/ifd/exif/{ushort=36867}", L"/ifd/exif/{ushort=36867}",
        L"/app1/ifd/{ushort=306}",        L"/ifd/{ushort=306}",
    };
    for (const wchar_t* path : kPaths) {
        PROPVARIANT v;
        PropVariantInit(&v);
        std::wstring result;
        if (SUCCEEDED(reader->GetMetadataByName(path, &v)) && v.vt == VT_LPSTR && v.pszVal) {
            result = FormatExifDate(v.pszVal);
        }
        PropVariantClear(&v);
        if (!result.empty()) return result;
    }
    return L"";
}
