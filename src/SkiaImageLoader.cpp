// SkiaImageLoader.cpp
// Implementation of Skia image loading and color management for HDR screensaver.

#include "SkiaImageLoader.h"
#include "GainMapProcessor.h"
#include "Logger.h"

// Skia headers
#include <include/core/SkImage.h>
#include <include/core/SkData.h>
#include <include/core/SkColorSpace.h>
#include <include/core/SkSurface.h>
#include <include/core/SkPixmap.h>
#include <include/codec/SkCodec.h>
#include <include/core/SkBitmap.h>
#include <include/core/SkStream.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkPaint.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkSamplingOptions.h>
#include <src/codec/SkJpegCodec.h>

// Standard library includes
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <future>
#include <algorithm>
#include <cassert>
#include <codecvt>

// Windows includes
#define NOMINMAX
#include <windows.h>
#undef min
#undef max

const bool kUseTestPattern = false;
const bool kLogEmbeddedGamut = false;

LoadedImageTriple LoadImageWithSkia(const std::wstring& imagePathW) {
    LoadedImageTriple triple;
    const auto initialTime = std::chrono::steady_clock::now();

    if (kUseTestPattern) {
        // --- Synthetic test pattern: two rectangles, SDR and HDR ---
        const int width = 800, height = 400;
        SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_F16_SkColorType, kOpaque_SkAlphaType, SkColorSpace::MakeSRGBLinear());
        SkBitmap bitmap;
        bitmap.allocPixels(info);
        SkCanvas canvas(bitmap);
        canvas.clear(SK_ColorBLACK);
        SkPaint paint;

        // SDR rectangle (left): R,G,B,White, all in [0,1]
        paint.setColor4f({1,0,0,1}); canvas.drawRect({10,10,190,190}, paint); // Red
        paint.setColor4f({0,1,0,1}); canvas.drawRect({210,10,390,190}, paint); // Green
        paint.setColor4f({0,0,1,1}); canvas.drawRect({410,10,590,190}, paint); // Blue
        paint.setColor4f({1,1,1,1}); canvas.drawRect({610,10,790,190}, paint); // White

        // HDR rectangle (right): R,G,B,White, all in [0,4] (4x peak, should be clipped to HDR white)
        paint.setColor4f({4,0,0,1}); canvas.drawRect({10,210,190,390}, paint); // Red HDR
        paint.setColor4f({0,4,0,1}); canvas.drawRect({210,210,390,390}, paint); // Green HDR
        paint.setColor4f({0,0,4,1}); canvas.drawRect({410,210,590,390}, paint); // Blue HDR
        paint.setColor4f({4,4,4,1}); canvas.drawRect({610,210,790,390}, paint); // White HDR

        // Copy to output buffer
        const size_t totalBytes = bitmap.computeByteSize();

        // Fill SDR image and gain map with the same test pattern
        for (const auto img : {triple.sdr, triple.gainMap}) {
            img->width = width;
            img->height = height;
            img->rowBytes = bitmap.rowBytes();
            img->pixels = std::make_unique<uint16_t[]>(totalBytes / sizeof(uint16_t));
            memcpy(img->pixels.get(), bitmap.getPixels(), totalBytes);
        }
        LOG_MSG("[LOG] Skia: Generated synthetic SDR/HDR test pattern.");
        return triple;
    }

    // Allocate output buffers
    const std::string imagePath = std::wstring_convert<std::codecvt_utf8<wchar_t>>().to_bytes(imagePathW);
    SkFILEStream stream(imagePath.c_str());
    if (!stream.isValid()) {
        LOG_MSG(L"[ERROR] Skia: Failed to open file: '", imagePathW, L"'");
        return triple;
    }
    std::unique_ptr<SkCodec> codec(SkCodec::MakeFromStream(std::make_unique<SkFILEStream>(imagePath.c_str())));
    if (!codec) {
        LOG_MSG(L"[ERROR] Skia: Failed to create codec for file: '", imagePathW, L"'");
        return triple;
    }

    if (kLogEmbeddedGamut) {
        const SkImageInfo& info = codec->getInfo();
        sk_sp<SkColorSpace> embeddedCS = info.refColorSpace();
        if (embeddedCS) {
            if (embeddedCS->isSRGB()) {
                LOG_MSG("[LOG] Skia: Embedded gamut: sRGB");
            } else {
                LOG_MSG("[LOG] Skia: Embedded gamut: NOT sRGB (likely P3 or other)");
            }
            // Try to get ICC profile size (if available)
            sk_sp<SkData> icc = embeddedCS->serialize();
            if (icc) {
                LOG_MSG(L"[LOG] Skia: Embedded ICC profile size: ", std::to_wstring(icc->size()));
            } else {
                LOG_MSG(L"[LOG] Skia: No ICC profile data available.");
            }
        } else {
            LOG_MSG("[LOG] Skia: No embedded color space found. Defaulting to sRGB.");
        }
    }

    const auto timeAfterFileIO = std::chrono::steady_clock::now();

    std::vector<uint8_t> sdrJpeg;
    std::vector<uint8_t> gainMapJpeg;
    GainMapProcessor::GainMapParams outParams;
    if (!GainMapProcessor().ExtractGainMap(imagePath, sdrJpeg, gainMapJpeg, outParams)) {
        LOG_MSG("[GainMap] Extraction failed");
        return triple;
    }

    triple.minGain = outParams.hdrMinValue;
    triple.maxGain = outParams.hdrMaxValue;
    triple.gamma = outParams.gamma;

    auto decodeJpeg = [&](const std::vector<uint8_t>& data, SkBitmap& targetBitmap) -> bool {
        sk_sp<SkData> skData = SkData::MakeWithoutCopy(data.data(), data.size());
        std::unique_ptr<SkCodec> codec(SkCodec::MakeFromData(skData));
        if (!codec)
            return false;
        SkImageInfo info = codec->getInfo().makeColorType(kRGBA_F16_SkColorType).makeColorSpace(SkColorSpace::MakeSRGBLinear());
        targetBitmap.allocPixels(info);
        if (codec->getPixels(info, targetBitmap.getPixels(), targetBitmap.rowBytes()) != SkCodec::kSuccess)
            return false;
        return true;
    };

    if (gainMapJpeg.empty()) {
        LOG_MSG("[GainMap] No gain map found, displaying SDR image");

        // Try to decode SDR only
        SkBitmap sdrBitmap;
        if (!decodeJpeg(sdrJpeg, sdrBitmap)) {
            LOG_MSG("[GainMap] Failed to decode SDR image or pixels.");
            return triple;
        }

        triple.sdr->width = sdrBitmap.width();
        triple.sdr->height = sdrBitmap.height();
        triple.sdr->rowBytes = sdrBitmap.rowBytes();
        size_t totalBytes = sdrBitmap.computeByteSize();
        triple.sdr->pixels = std::make_unique<uint16_t[]>(totalBytes / sizeof(uint16_t));
        triple.hasGainMap = false;
        memcpy(triple.sdr->pixels.get(), sdrBitmap.getPixels(), totalBytes);
        return triple;
    }
    triple.hasGainMap = true;

    const auto timeAfterXMP = std::chrono::steady_clock::now();

    // Parallel decode: SDR and gain map
    SkBitmap sdrBitmap;
    SkBitmap gainBitmap;
    std::chrono::steady_clock::time_point timeAfterSdrDecode;
    std::chrono::steady_clock::time_point timeAfterGmDecode;

    auto sdrFuture = std::async(std::launch::async, [&]() {
        const bool success = decodeJpeg(sdrJpeg, sdrBitmap);
        timeAfterSdrDecode = std::chrono::steady_clock::now();
        return success;
    });

    auto gainFuture = std::async(std::launch::async, [&]() {
        const bool success = decodeJpeg(gainMapJpeg, gainBitmap);
        timeAfterGmDecode = std::chrono::steady_clock::now();
        return success;
    });

    const bool sdrOk = sdrFuture.get();
    const bool gainOk = gainFuture.get();
    const auto timeAfterDecode = std::chrono::steady_clock::now();

    if (!sdrOk) {
        LOG_MSG("[GainMap] Failed to decode SDR image or pixels.");
        return triple;
    }

    if (!gainOk) {
        LOG_MSG("[GainMap] Failed to decode gain map image or pixels.");
        return triple;
    }

    // Check dimensions
    const int width = sdrBitmap.width();
    const int height = sdrBitmap.height();
    if (width != gainBitmap.width() || height != gainBitmap.height()) {
        LOG_MSG("[GainMap] Main and gain map image dimensions do not match.");
        return triple;
    }

    const auto timeAfterGmMath = std::chrono::steady_clock::now();

    // --- Fill SDR output ---
    triple.sdr->width = width;
    triple.sdr->height = height;
    triple.sdr->rowBytes = sdrBitmap.rowBytes();
    size_t totalBytes = sdrBitmap.rowBytes() * height;
    triple.sdr->pixels = std::make_unique<uint16_t[]>(totalBytes / sizeof(uint16_t));
    memcpy(triple.sdr->pixels.get(), sdrBitmap.getPixels(), totalBytes);

    // --- Fill GainMap output ---
    triple.gainMap->width = width;
    triple.gainMap->height = height;
    triple.gainMap->rowBytes = gainBitmap.rowBytes();
    triple.gainMap->pixels = std::make_unique<uint16_t[]>(totalBytes / sizeof(uint16_t));
    memcpy(triple.gainMap->pixels.get(), gainBitmap.getPixels(), totalBytes);

    const auto timeAfterWriteOut = std::chrono::steady_clock::now();

    const bool kPrintTiming = false;
    if (kPrintTiming) {
        LOG_MSG(L"[PROFILE] File I/O         : ", std::to_wstring(std::chrono::duration_cast<std::chrono::milliseconds>(timeAfterFileIO-initialTime).count()), L" ms");
        LOG_MSG(L"[PROFILE] XMP analysis     : ", std::to_wstring(std::chrono::duration_cast<std::chrono::milliseconds>(timeAfterXMP-timeAfterFileIO).count()), L" ms");
        LOG_MSG(L"[PROFILE] Total decode     : ", std::to_wstring(std::chrono::duration_cast<std::chrono::milliseconds>(timeAfterDecode-timeAfterXMP).count()), L" ms");
        LOG_MSG(L"[PROFILE]    SDR decode    : ", std::to_wstring(std::chrono::duration_cast<std::chrono::milliseconds>(timeAfterSdrDecode-timeAfterXMP).count()), L" ms");
        LOG_MSG(L"[PROFILE]    GainMap decode: ", std::to_wstring(std::chrono::duration_cast<std::chrono::milliseconds>(timeAfterGmDecode-timeAfterXMP).count()), L" ms");
        LOG_MSG(L"[PROFILE] GainMap math     : ", std::to_wstring(std::chrono::duration_cast<std::chrono::milliseconds>(timeAfterGmMath-timeAfterDecode).count()), L" ms");
        LOG_MSG(L"[PROFILE] Write image      : ", std::to_wstring(std::chrono::duration_cast<std::chrono::milliseconds>(timeAfterWriteOut-timeAfterGmMath).count()), L" ms");
    }

    return triple;
}
