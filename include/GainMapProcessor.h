#pragma once
#include <vector>
#include <string>
#include <memory>
#include <cstdint>

// Responsible for extracting the base JPEG and the HDR gain map from a JPEG file
class GainMapProcessor {
public:
    struct GainMapParams {
        float hdrMinValue = 0.0f; // Not in our JPEGs.
        float hdrMaxValue = 4.0f;
        float gamma = 1.0f; // Not in our JPEGs.
        float sdrBrightness = 0.f;
        float sdrContrast = 0.f;
        float sdrClarity = 0.f;
        float sdrHighlights = 0.f;
        float sdrShadows = 0.f;
        float sdrWhites = 0.f;
        float sdrBlend = 0.f;

        // https://exiftool.org/TagNames/XMP.html#hdrgm
        // Only "Version" is found in our JPEGs :(
        bool baseRenditionIsHDR;
        double GainMapMax;
        double GainMapMin;
        double Gamma;
        float HDRCapacityMax;
        float HDRCapacityMin;
        double OffsetHDR;
        double OffsetSDR;
        std::string Version;
    };

    // Extracts the SDR and gain map JPEG images and parameters from a JPEG file.
    // Returns true on success, false on failure.
    bool ExtractGainMap(const std::string& jpegPath, std::vector<uint8_t>& outSDRJpeg, std::vector<uint8_t>& outGainMapJpeg, GainMapParams& outParams);
};
