#include "TestPattern.h"
#include "LoadedImageTypes.h"

TestPattern GenerateTestPattern(bool hdr, int width, int height) {
    TestPattern pattern;
    pattern.width = width;
    pattern.height = height;
    pattern.rowBytes = width * 4 * sizeof(uint16_t);
    pattern.pixels = std::make_unique<uint16_t[]>(width * height * 4);
    float sdrVal = 1.0f;
    float hdrVal = hdr ? 4.0f : 1.0f;

    // Fill with rectangles: Red, Green, Blue, White (HDR top, SDR bottom)
    const int numBlocks = 10;
    const int segmentWidth = width / 4;
    const int blockWidth = segmentWidth / numBlocks;

    for (int y = 0; y < height; ++y) {
        const float val = (y < height / 2) ? hdrVal : sdrVal;
        for (int x = 0; x < width; ++x) {
            const int idx = (y * width + x) * 4;
            const int blockIdx = (x % segmentWidth) / blockWidth;
            const float brightness = val * (1.0f - blockIdx / 9.0f); // 10 levels

            if (x < segmentWidth) {
                // Red
                pattern.pixels[idx+0] = floatToHalf(brightness);
                pattern.pixels[idx+1] = 0;
                pattern.pixels[idx+2] = 0;
            } else if (x < 2 * segmentWidth) {
                // Green
                pattern.pixels[idx+0] = 0;
                pattern.pixels[idx+1] = floatToHalf(brightness);
                pattern.pixels[idx+2] = 0;
            } else if (x < 3 * segmentWidth) {
                // Blue
                pattern.pixels[idx+0] = 0;
                pattern.pixels[idx+1] = 0;
                pattern.pixels[idx+2] = floatToHalf(brightness);
            } else {
                // White
                pattern.pixels[idx+0] = floatToHalf(brightness);
                pattern.pixels[idx+1] = floatToHalf(brightness);
                pattern.pixels[idx+2] = floatToHalf(brightness);
            }
            pattern.pixels[idx+3] = floatToHalf(1.0f); // Alpha
        }
    }

    return pattern;
}

LoadedImageTriple createTripleFromPattern(const TestPattern& pattern) {
    // Create vectors for the two images.
    std::vector<uint8_t> linearData(pattern.width * pattern.height * 4, 0);
    std::vector<uint8_t> gammaData(pattern.width * pattern.height * 4, 0);
    std::vector<uint8_t> gainMapData(pattern.width * pattern.height * 4, 0);
    for (int y = 0; y < pattern.height; ++y)
    {
        for (int x = 0; x < pattern.width; ++x)
        {
            int idx = (y * pattern.width + x) * 4;

            const uint16_t rh = pattern.pixels[idx + 0];
            const uint16_t gh = pattern.pixels[idx + 1];
            const uint16_t bh = pattern.pixels[idx + 2];
            const uint16_t ah = pattern.pixels[idx + 3];
            const float r = halfToFloat(rh);
            const float g = halfToFloat(gh);
            const float b = halfToFloat(bh);
            const float a = halfToFloat(ah);

            linearData[idx + 0] = floatToByte(r);
            linearData[idx + 1] = floatToByte(g);
            linearData[idx + 2] = floatToByte(b);
            linearData[idx + 3] = floatToByte(a);

            // Create the gamma-encoded version.
            const float rEnc = encodeGamma(r);
            const float gEnc = encodeGamma(g);
            const float bEnc = encodeGamma(b);

            gammaData[idx + 0] = floatToByte(rEnc);
            gammaData[idx + 1] = floatToByte(gEnc);
            gammaData[idx + 2] = floatToByte(bEnc);
            gammaData[idx + 3] = floatToByte(a);

            // Create the gain map.

            // Read SDR image. This should be gamma 1.0.
            const float rsdr = byteToFloat(linearData[idx + 0]);
            const float gsdr = byteToFloat(linearData[idx + 1]);
            const float bsdr = byteToFloat(linearData[idx + 2]);
            // Offset parameters, which need to be written to the
            // Gain Map metadata so that the transform can be reversed later.
            const float k_sdr = 4.f;
            const float k_hdr = 4.f;
            // Calculate gain map in log2 space.
            const float rgm = std::log2((r + k_hdr) / (rsdr + k_sdr));
            const float ggm = std::log2((g + k_hdr) / (gsdr + k_sdr));
            const float bgm = std::log2((b + k_hdr) / (bsdr + k_sdr));

            gainMapData[idx + 0] = floatToByte(rgm);
            gainMapData[idx + 1] = floatToByte(ggm);
            gainMapData[idx + 2] = floatToByte(bgm);
            gainMapData[idx + 3] = floatToByte(a);
        }
    }

    LoadedImageTriple triple;

    triple.sdr->height = pattern.height;
    triple.sdr->width = pattern.width;
    triple.sdr->rowBytes = pattern.width * 4 * sizeof(uint16_t);
    triple.sdr->pixels = std::make_unique<uint16_t[]>(pattern.width * pattern.height * 4);

    for (int y = 0; y < pattern.height; ++y)
    {
        for (int x = 0; x < pattern.width; ++x)
        {
            const size_t idx = (y * pattern.width + x) * 4;
            #if 1
            triple.sdr->pixels[idx + 0] = byteToHalf(linearData[idx + 0]);
            triple.sdr->pixels[idx + 1] = byteToHalf(linearData[idx + 1]);
            triple.sdr->pixels[idx + 2] = byteToHalf(linearData[idx + 2]);
            triple.sdr->pixels[idx + 3] = byteToHalf(linearData[idx + 3]);
            #else
            triple.sdr->pixels[idx + 0] = byteToHalf(gammaData[idx + 0]);
            triple.sdr->pixels[idx + 1] = byteToHalf(gammaData[idx + 1]);
            triple.sdr->pixels[idx + 2] = byteToHalf(gammaData[idx + 2]);
            triple.sdr->pixels[idx + 3] = byteToHalf(gammaData[idx + 3]);
            #endif
        }
    }

    triple.gainMap->height = pattern.height;
    triple.gainMap->width = pattern.width;
    triple.gainMap->rowBytes = pattern.width * 4 * sizeof(uint16_t);
    triple.gainMap->pixels = std::make_unique<uint16_t[]>(pattern.width * pattern.height * 4);

    for (int y = 0; y < pattern.height; ++y) {
        for (int x = 0; x < pattern.width; ++x) {
            const size_t idx = (y * pattern.width + x) * 4;
            triple.gainMap->pixels[idx + 0] = byteToHalf(gainMapData[idx + 0]);
            triple.gainMap->pixels[idx + 1] = byteToHalf(gainMapData[idx + 1]);
            triple.gainMap->pixels[idx + 2] = byteToHalf(gainMapData[idx + 2]);
            triple.gainMap->pixels[idx + 3] = byteToHalf(gainMapData[idx + 3]);
        }
    }

    return triple;
}
