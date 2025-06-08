#include "GdiPlusImageLoader.h"
#include "GainMapProcessor.h"
#include "TestPattern.h"
#include "Logger.h"

#include <windows.h>
#include <gdiplus.h>
#include <cstdint>
#include <memory>
#include <string>
#include <codecvt>
#include <iomanip> // For std::setw and std::setfill
#include <algorithm> // For std::min/std::max
#include <future>
#include <vector>
#include <cmath>
#include <chrono>

#include <omp.h>

// Helper: Read big-endian 32-bit value
uint32_t read_be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// Helper: Print ICC color space and linearity
void LogJpegColorSpace(const std::vector<uint8_t>& jpegData, const std::wstring& prefix) {
    size_t i = 0;
    while (i + 4 < jpegData.size()) {
        // Look for marker: 0xFF, marker type
        if (jpegData[i] == 0xFF && jpegData[i + 1] == 0xE2) { // APP2
            uint16_t segmentLen = (jpegData[i + 2] << 8) | jpegData[i + 3];
            if (i + 4 + 11 < jpegData.size()) {
                std::string id(reinterpret_cast<const char*>(&jpegData[i + 4]), 11);
                if (id == "ICC_PROFILE") {
                    // ICC profile starts at i + 4 + 14 (11 bytes + 3 bytes header)
                    size_t iccStart = i + 4 + 14;
                    if (iccStart + 128 < jpegData.size()) {
                        const uint8_t* icc = &jpegData[iccStart];
                        std::string deviceModel(reinterpret_cast<const char*>(icc + 48), 4);
                        std::string colorSpace(reinterpret_cast<const char*>(icc + 16), 4);
                        std::string pcs(reinterpret_cast<const char*>(icc + 20), 4);

                        std::wstring colorDesc;
                        if (deviceModel == "sRGB") colorDesc = L"sRGB";
                        else if (deviceModel == "APPL") colorDesc = L"Display P3";
                        else if (deviceModel == "ADBE") colorDesc = L"AdobeRGB";
                        else colorDesc = std::wstring(deviceModel.begin(), deviceModel.end()) + L" (unknown)";

                        LOG_MSG(prefix + L"ICC profile device model: ", colorDesc);
                        LOG_MSG(prefix + L"ICC profile color space: ", std::wstring(colorSpace.begin(), colorSpace.end()));
                        LOG_MSG(prefix + L"ICC profile PCS: ", std::wstring(pcs.begin(), pcs.end()));

                        // Heuristic: check for linearity by searching for 'lini' tag in the profile
                        bool isLinear = false;
                        // ICC tag table starts at byte 128, next 4 bytes = tag count
                        if (iccStart + 132 < jpegData.size()) {
                            uint32_t tagCount = read_be32(icc + 128);
                            for (uint32_t t = 0; t < tagCount; ++t) {
                                size_t tagOffset = 132 + t * 12;
                                if (iccStart + tagOffset + 4 < jpegData.size()) {
                                    std::string tagSig(reinterpret_cast<const char*>(icc + tagOffset), 4);
                                    if (tagSig == "kTRC" || tagSig == "gTRC" || tagSig == "bTRC" || tagSig == "rTRC") {
                                        // Check if the tag type is 'lini' (linear)
                                        uint32_t tagDataOffset = read_be32(icc + tagOffset + 4);
                                        if (iccStart + tagDataOffset + 4 < jpegData.size()) {
                                            std::string tagType(reinterpret_cast<const char*>(icc + tagDataOffset), 4);
                                            if (tagType == "lini") {
                                                isLinear = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        LOG_MSG(prefix + L"ICC profile transfer: ", isLinear ? L"linear" : L"gamma-encoded");
                        return;
                    }
                }
            }
            i += 2 + segmentLen;
        } else {
            ++i;
        }
    }
    LOG_MSG(prefix + L"JPEG does not contain an embedded ICC profile.");
}

// Helper: Create IStream from std::vector<uint8_t>
std::unique_ptr<IStream, void(*)(IStream*)> CreateStreamFromBuffer(const std::vector<uint8_t>& buffer) {
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, buffer.size());
    void* pMem = ::GlobalLock(hMem);
    memcpy(pMem, buffer.data(), buffer.size());
    ::GlobalUnlock(hMem);

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(hMem, TRUE, &stream))) {
        ::GlobalFree(hMem);
        return {nullptr, [](IStream*){}};
    }
    return {stream, [](IStream* s){ if(s) s->Release(); }};
}

std::unique_ptr<Gdiplus::Bitmap> LoadBitmapFromBuffer(const std::vector<uint8_t>& data, bool useEmbeddedColorManagement) {
    auto stream = CreateStreamFromBuffer(data);
    if (!stream)
        return nullptr;
    return std::make_unique<Gdiplus::Bitmap>(stream.get(), (BOOL)useEmbeddedColorManagement);
}

static bool createTripleFromBuffer(const std::vector<uint8_t>& buffer, std::shared_ptr<LoadedImage> targetImage, bool useEmbeddedColorManagement, bool isGainMap) {
    auto bitmap = LoadBitmapFromBuffer(buffer, useEmbeddedColorManagement);
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        return false;
    }

    const int width = bitmap->GetWidth();
    const int height = bitmap->GetHeight();
    targetImage->width = width;
    targetImage->height = height;
    targetImage->rowBytes = width * 4 * sizeof(uint16_t);
    targetImage->pixels = std::make_unique<uint16_t[]>(width * height * 4);

    Gdiplus::BitmapData bmpData;
    Gdiplus::Rect rect(0, 0, width, height);
    if (!bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bmpData) == Gdiplus::Ok) {
        return false;
    }

    // Precompute LUT (thread-safe static). This is for performance reasons,
    // it makes the loop below significantly faster.
    // TODO: No color management, just sRGB to linear Rec.2020 (approximate)
    //       For real Rec.2020, use a color management library.
    static float srgbLUT[256];
    static std::once_flag lutInitFlag;
    std::call_once(lutInitFlag, [] {
        for (int i = 0; i < 256; ++i) {
            float c = i / 255.0f;
            srgbLUT[i] = (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
    });

    #pragma omp parallel for
    for (int y = 0; y < height; ++y) {
        uint8_t* src = (uint8_t*)bmpData.Scan0 + y * bmpData.Stride;
        for (int x = 0; x < width; ++x) {
            const uint8_t b = src[x * 4 + 0];
            const uint8_t g = src[x * 4 + 1];
            const uint8_t r = src[x * 4 + 2];
            const uint8_t a = src[x * 4 + 3];

            const size_t idx = (y * width + x) * 4;

            if (isGainMap) {
                // First, create the pixels for displaying the gain map.
                #if 1 // OLD -> gain map displays image, but why? no delinearization here...
                targetImage->pixels[idx + 0] = byteToHalf(r);
                targetImage->pixels[idx + 1] = byteToHalf(g);
                targetImage->pixels[idx + 2] = byteToHalf(b);
                targetImage->pixels[idx + 3] = byteToHalf(a);
                #elif 0 // NEW -> skip unnecessary conversions -> gain map displays completely black
                targetImage->pixels[idx + 0] = r;
                targetImage->pixels[idx + 1] = g;
                targetImage->pixels[idx + 2] = b;
                targetImage->pixels[idx + 3] = a;
                // This makes the gain map be barely visible.
                targetImage->pixels[idx + 0] += floatToHalf(0.5f);
                targetImage->pixels[idx + 1] += floatToHalf(0.5f);
                targetImage->pixels[idx + 2] += floatToHalf(0.5f);
                targetImage->pixels[idx + 3] += floatToHalf(0.5f);
                #else // NEW 2 -> gain map is linear! -> have to de-linearize for it to show an image!
                const float rf = srgbLUT[r];
                const float gf = srgbLUT[g];
                const float bf = srgbLUT[b];
                targetImage->pixels[idx + 0] = floatToHalf(rf);
                targetImage->pixels[idx + 1] = floatToHalf(gf);
                targetImage->pixels[idx + 2] = floatToHalf(bf);
                targetImage->pixels[idx + 3] = a;
                #endif
                continue;
            }

            float rf, gf, bf;
            if (useEmbeddedColorManagement) {
                // Linearize.
                rf = srgbLUT[r];
                gf = srgbLUT[g];
                bf = srgbLUT[b];
            } else {
                // Linearize.
                displayP3ToLinearRec2020(byteToFloat(r), byteToFloat(g), byteToFloat(b), rf, gf, bf);
            }
            const float af = a / 255.0f;

            // These are linearized!
            targetImage->pixels[idx + 0] = floatToHalf(rf);
            targetImage->pixels[idx + 1] = floatToHalf(gf);
            targetImage->pixels[idx + 2] = floatToHalf(bf);
            targetImage->pixels[idx + 3] = floatToHalf(af);
        }
    }

    const Gdiplus::Status st = bitmap->UnlockBits(&bmpData);
    if (st != Gdiplus::Status::Ok) {
        return false;
    }

    return true;
};


LoadedImageTriple LoadImageWithGdiPlus(const std::wstring& imagePathW) {
    LoadedImageTriple triple;
    const auto initialTime = std::chrono::steady_clock::now();

    const std::string imagePath = std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(imagePathW);

    std::vector<uint8_t> sdrJpeg;
    std::vector<uint8_t> gainMapJpeg;
    GainMapProcessor::GainMapParams outParams;
    if (!GainMapProcessor().ExtractGainMap(imagePath, sdrJpeg, gainMapJpeg, outParams)) {
        LOG_MSG("[GainMap] Extraction failed");
        return triple;
    }

    LogJpegColorSpace(sdrJpeg, L"SDR ");
    LogJpegColorSpace(gainMapJpeg, L"GainMap ");

    const auto timeAfterExtraction = std::chrono::steady_clock::now();

    triple.minGain = outParams.hdrMinValue;
    triple.maxGain = outParams.hdrMaxValue;
    triple.gamma = outParams.gamma;
    triple.hasGainMap = !gainMapJpeg.empty();

    const bool kUseTestImage = false;
    if (kUseTestImage) {
        // Compute width/height
        int width;
        int height;
        {
            Gdiplus::GdiplusStartupInput gdiplusStartupInput2;
            ULONG_PTR gdiplusToken2 = 0;
            Gdiplus::GdiplusStartup(&gdiplusToken2, &gdiplusStartupInput2, nullptr);
            {
                auto bitmap = LoadBitmapFromBuffer(sdrJpeg, true);
                if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
                    return triple;
                }
                width = bitmap->GetWidth();
                height = bitmap->GetHeight();
            }
            Gdiplus::GdiplusShutdown(gdiplusToken2);
        }


        // Fill the vectors with a simple gradient and saturation boost.
        // Colors are computed in linear Display P3 space.
        const TestPattern pattern = GenerateTestPattern(true, width, height);
        return createTripleFromPattern(pattern);
    }

    std::chrono::steady_clock::time_point timeAfterSdrDecode;
    std::chrono::steady_clock::time_point timeAfterGmDecode;

    // Parallel decode: SDR and gain map (if any)
    {
        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        ULONG_PTR gdiplusToken = 0;
        Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

        auto sdrFuture = std::async(std::launch::async, [&]() {
            // The SDR image should have embedded color management. If we set
            // this to 'true', GDI+ will convert to non-linear sRGB. Otherwise,
            // it will remain whatever it is (non-linear Display P3 in our case).
            const bool useEmbeddedColorManagement = true; // both work now, false seems more washed out
            // We've got gamma-encoded sRGB/P3 so we have to linearize.
            const bool isGainMap = false;
            const bool success = createTripleFromBuffer(sdrJpeg, triple.sdr, useEmbeddedColorManagement, isGainMap);
            timeAfterSdrDecode = std::chrono::steady_clock::now();
            return success;
        });

        auto gainFuture = std::async(std::launch::async, [&]() {
            if (!triple.hasGainMap)
                return true;
            // The gainmap should not have any embedded color management.
            const bool useEmbeddedColorManagement = false; // doesn't matter, nothing embedded anyway
            // The gainmap should be linear already.
            const bool isGainMap = true;
            const bool success = createTripleFromBuffer(gainMapJpeg, triple.gainMap, useEmbeddedColorManagement, isGainMap);
            timeAfterGmDecode = std::chrono::steady_clock::now();
            return success;
        });

        const bool sdrOk = sdrFuture.get();
        const bool gainOk = gainFuture.get();

        Gdiplus::GdiplusShutdown(gdiplusToken);

        if (!sdrOk) {
            LOG_MSG("[GainMap] Failed to decode SDR image.");
            return triple;
        }

        if (!gainOk) {
            LOG_MSG("[GainMap] Failed to decode gain map.");
            return triple;
        }
    }

    const auto timeAfterDecode = std::chrono::steady_clock::now();

    // Stop here for SDR JPEGs.
    if (!triple.hasGainMap) {
        return triple;
    }

    // Check dimensions
    if (triple.sdr->width != triple.gainMap->width || triple.sdr->height != triple.gainMap->height) {
        LOG_MSG("[GainMap] Main and gain map image dimensions do not match.");
        return triple;
    }

    const auto timeAfterCompositing = std::chrono::steady_clock::now();

    const bool kPrintTiming = false;
    if (kPrintTiming) {
        LOG_MSG(L"[PROFILE] SDR/GainMap extract: ", std::to_wstring(std::chrono::duration_cast<std::chrono::milliseconds>(timeAfterExtraction-initialTime).count()), L" ms");
        LOG_MSG(L"[PROFILE] Total decode       : ", std::to_wstring(std::chrono::duration_cast<std::chrono::milliseconds>(timeAfterDecode-timeAfterExtraction).count()), L" ms");
        LOG_MSG(L"[PROFILE]    SDR decode      : ", std::to_wstring(std::chrono::duration_cast<std::chrono::milliseconds>(timeAfterSdrDecode-timeAfterExtraction).count()), L" ms");
        LOG_MSG(L"[PROFILE]    GainMap decode  : ", std::to_wstring(std::chrono::duration_cast<std::chrono::milliseconds>(timeAfterGmDecode-timeAfterExtraction).count()), L" ms");
        LOG_MSG(L"[PROFILE] HDR compositing    : ", std::to_wstring(std::chrono::duration_cast<std::chrono::milliseconds>(timeAfterCompositing-timeAfterDecode).count()), L" ms");
    }

    return triple;
}
