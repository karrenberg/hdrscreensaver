#include "LoadedImageTypes.h"
#include <memory>
#include <bit>
#include <cmath>

#include <omp.h>

size_t LoadedImageTriple::sizeInBytes() const {
    size_t sz = 0;
    auto img_bytes = [](const LoadedImage& img) {
        return img.pixels ? (size_t(img.width) * img.height * 4 * sizeof(uint16_t)) : 0;
    };
    sz += img_bytes(*sdr);
    sz += img_bytes(*gainMap);
    return sz;
}

inline float lerp(float start, float end, float t) {
    return start + (end - start) * t;
}

// Composite SDR & gainmap to create HDR output.
std::shared_ptr<LoadedImage> LoadedImageTriple::createHDR() const {
    std::shared_ptr<LoadedImage> hdr = std::make_shared<LoadedImage>();
    hdr->width = sdr->width;
    hdr->height = sdr->height;
    hdr->rowBytes = sdr->rowBytes;
    const size_t totalBytes = hdr->rowBytes * hdr->height;
    hdr->pixels = std::make_unique<uint16_t[]>(totalBytes / sizeof(uint16_t));

    #pragma omp parallel for
    for (int y = 0; y < sdr->height; ++y) {
        for (int x = 0; x < sdr->width; ++x) {
            const size_t idx = (y * sdr->width + x) * 4;
            for (int c = 0; c < 3; ++c) {
                #if 0
                // NOTE: Precision is really important here, the multiplications don't work in half!
                const float sdrVal = halfToFloat(sdr->pixels[idx + c]);
                const float g = halfToFloat(gainMap->pixels[idx + c]);

                //const float G = lerp(minGain, maxGain, std::pow(g, 1.f / gamma));
                const float G = lerp(minGain, maxGain+1.f, std::pow(g, 1.f / gamma)); // HACK!
                const float k_sdr = 0.0f;
                const float k_hdr = 0.0f;
                const float hdrVal = (sdrVal + k_sdr) * std::exp2f(G) - k_hdr;
                hdr->pixels[idx + c] = floatToHalf(hdrVal);
                #else
                // Optimized
                const float sdrVal = halfToFloat(sdr->pixels[idx + c]);
                const float g = halfToFloat(gainMap->pixels[idx + c]);
                const float G = lerp(minGain, maxGain+1.f, g); // +HACK! +Optimized
                const float hdrVal = sdrVal * fast_exp2f(G); // +Optimized
                hdr->pixels[idx + c] = floatToHalf(hdrVal);
                #endif
            }
            // Copy alpha
            hdr->pixels[idx + 3] = sdr->pixels[idx + 3];
        }
    }
    return hdr;
}