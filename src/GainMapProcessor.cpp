// GainMapProcessor.cpp
// Implements gain map extraction and application for HDR JPEGs with embedded gain maps.
// See Gain_Map_1_0d15.pdf for algorithm details.

#include "GainMapProcessor.h"
#include "Logger.h"

#include <iostream>
#include <fstream>
#include <cassert>
#include <string>
#include <memory> // needed?
#include <cmath> // needed?

// Integrate Adobe XMP Toolkit SDK for robust XMP parsing
// Must be defined to instantiate template classes
#define TXMP_STRING_TYPE std::string 

// Must be defined to give access to XMPFiles
#define XMP_INCLUDE_XMPFILES 1 

// Ensure XMP templates are instantiated
#include <public/include/XMP.incl_cpp>

// Provide access to the API
#include <public/include/XMP.hpp>

// TODO: Parse MPF segment to extract secondary (gain map) image
// TODO: Parse gain map parameters from XMP
// TODO: Implement gain map application as per PDF

bool GainMapProcessor::ExtractGainMap(const std::string& jpegPath, std::vector<uint8_t>& outSDRJpeg, std::vector<uint8_t>& outGainMapJpeg, GainMapParams& outParams) {
    // Step 1: Read the JPEG file into memory
    std::ifstream file(jpegPath, std::ios::binary);
    if (!file) {
        LOG_MSG(L"[GainMap] ERROR: Could not open file.");
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), {});
    if (data.size() < 4) {
        LOG_MSG(L"[GainMap] ERROR: File too small.");
        return false;
    }
    struct MarkerInfo {
        size_t offset;
        uint8_t marker;
        uint16_t segLen;
    };
    std::vector<MarkerInfo> markers;
    for (size_t i = 0; i + 1 < data.size(); ) {
        if (data[i] == 0xFF && data[i+1] != 0x00) {
            uint8_t marker = data[i+1];
            uint16_t segLen = 0;
            if (marker != 0xD8 && marker != 0xD9 && (marker < 0xD0 || marker > 0xD7)) {
                if (i + 3 < data.size()) {
                    segLen = (data[i+2] << 8) | data[i+3];
                }
            }
            markers.push_back({i, marker, segLen});
            i += 2;
            if (segLen > 0) {
                i += segLen;
            }
        } else {
            ++i;
        }
    }

    std::vector<size_t> soiOffsets, eoiOffsets;
    for (const auto& m : markers) {
        if (m.marker == 0xD8) soiOffsets.push_back(m.offset);
        if (m.marker == 0xD9) eoiOffsets.push_back(m.offset + 2);
    }

    if (soiOffsets.empty() || eoiOffsets.empty()) {
        LOG_MSG(L"[GainMap] ERROR: No SOI/EOI marker found.");
        return false;
    }
    outSDRJpeg.assign(data.begin() + soiOffsets[0], data.begin() + eoiOffsets[0]);

    if (soiOffsets.size() < 2 || eoiOffsets.size() < 2) {
        LOG_MSG(L"[GainMap] Less than 2 SOI/EOI markers - no gain map found, will only display SDR image.");
        return true;
    } else {
        outGainMapJpeg.assign(data.begin() + soiOffsets[1], data.begin() + eoiOffsets[1]);
    }

    // XMP extraction
    try {
        if (!SXMPMeta::Initialize()) {
            LOG_MSG(L"[XMP] Failed to initialize XMP Toolkit.");
            return false;
        }
        if (!SXMPFiles::Initialize()) {
            LOG_MSG(L"[XMP] Failed to initialize SXMPFiles.");
            SXMPMeta::Terminate();
            return false;
        }
        SXMPFiles myFile;
        XMP_OptionBits opts = kXMPFiles_OpenForRead;
        bool ok = myFile.OpenFile(jpegPath.c_str(), kXMP_JPEGFile, opts);
        if (!ok) {
            LOG_MSG(L"[XMP] Could not open file for XMP reading: ", jpegPath.c_str());
            SXMPFiles::Terminate();
            SXMPMeta::Terminate();
            return false;
        }
        SXMPMeta meta;
        if (!myFile.GetXMP(&meta)) {
            LOG_MSG(L"[XMP] No XMP metadata found in file.");
            myFile.CloseFile();
            SXMPFiles::Terminate();
            SXMPMeta::Terminate();
            return false;
        }
        std::string xmpPacket;
        meta.SerializeToBuffer(&xmpPacket);

        // Extract relevant fields from the XMP (crs namespace)
        std::string ns_crs = "http://ns.adobe.com/camera-raw-settings/1.0/";
        SXMPMeta::RegisterNamespace(ns_crs.c_str(), "crs", nullptr);

        std::string value;

        if (meta.GetProperty(ns_crs.c_str(), "HDRMinValue", &value, nullptr)) {
            outParams.hdrMinValue = std::stof(value);
        }
        if (meta.GetProperty(ns_crs.c_str(), "HDRMaxValue", &value, nullptr)) {
            outParams.hdrMaxValue = std::stof(value);
        }
        if (meta.GetProperty(ns_crs.c_str(), "Gamma", &value, nullptr)) {
            outParams.gamma = std::stof(value);
        }
        if (meta.GetProperty(ns_crs.c_str(), "SDRBrightness", &value, nullptr)) {
            outParams.sdrBrightness = std::stof(value);
        }
        if (meta.GetProperty(ns_crs.c_str(), "SDRContrast", &value, nullptr)) {
            outParams.sdrContrast = std::stof(value);
        }
        if (meta.GetProperty(ns_crs.c_str(), "SDRClarity", &value, nullptr)) {
            outParams.sdrClarity = std::stof(value);
        }
        if (meta.GetProperty(ns_crs.c_str(), "SDRHighlights", &value, nullptr)) {
            outParams.sdrHighlights = std::stof(value);
        }
        if (meta.GetProperty(ns_crs.c_str(), "SDRShadows", &value, nullptr)) {
            outParams.sdrShadows = std::stof(value);
        }
        if (meta.GetProperty(ns_crs.c_str(), "SDRWhites", &value, nullptr)) {
            outParams.sdrWhites = std::stof(value);
        }
        if (meta.GetProperty(ns_crs.c_str(), "SDRBlend", &value, nullptr)) {
            outParams.sdrBlend = std::stof(value);
        }

        // Extract relevant fields from the XMP (crs namespace)
        // https://exiftool.org/TagNames/XMP.html#hdrgm
        std::string ns_crs_gm = "http://ns.adobe.com/hdr-gain-map/1.0/";
        SXMPMeta::RegisterNamespace(ns_crs_gm.c_str(), "crs", nullptr);

        if (meta.GetProperty(ns_crs_gm.c_str(), "baseRenditionIsHDR", &value, nullptr)) {
            outParams.baseRenditionIsHDR = std::stoi(value);
        }
        if (meta.GetProperty(ns_crs_gm.c_str(), "GainMapMax", &value, nullptr)) {
            outParams.GainMapMax = std::stod(value);
        }
        if (meta.GetProperty(ns_crs_gm.c_str(), "GainMapMin", &value, nullptr)) {
            outParams.GainMapMin = std::stod(value);
        }
        if (meta.GetProperty(ns_crs_gm.c_str(), "Gamma", &value, nullptr)) {
            outParams.Gamma = std::stod(value);
        }
        if (meta.GetProperty(ns_crs_gm.c_str(), "HDRCapacityMax", &value, nullptr)) {
            outParams.HDRCapacityMax = std::stof(value);
        }
        if (meta.GetProperty(ns_crs_gm.c_str(), "HDRCapacityMin", &value, nullptr)) {
            outParams.HDRCapacityMin = std::stof(value);
        }
        if (meta.GetProperty(ns_crs_gm.c_str(), "OffsetHDR", &value, nullptr)) {
            outParams.OffsetHDR = std::stod(value);
        }
        if (meta.GetProperty(ns_crs_gm.c_str(), "OffsetSDR", &value, nullptr)) {
            outParams.OffsetSDR = std::stod(value);
        }
        if (meta.GetProperty(ns_crs_gm.c_str(), "Version", &value, nullptr)) {
            outParams.Version = value;
        }

        myFile.CloseFile();
        SXMPFiles::Terminate();
        SXMPMeta::Terminate();
    } catch (const XMP_Error& e) {
        LOG_MSG(L"[XMP] XMP Toolkit error: ", e.GetErrMsg());
        SXMPFiles::Terminate();
        SXMPMeta::Terminate();
        return false;
    }

    return true;
}
