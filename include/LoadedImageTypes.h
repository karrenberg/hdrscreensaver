#pragma once
#include <memory>
#include <bit>
#include <DirectXPackedVector.h>
#include <vector>
#include <cmath> // expf etc.

/**
 * Display modes for HDR images
 */
enum class DisplayMode { 
    HDR,      ///< High Dynamic Range mode
    SDR,      ///< Standard Dynamic Range mode  
    GainMap   ///< Gain map visualization mode
};

/**
 * Represents a single image with RGBA F16 (half-float) pixel data
 */
struct LoadedImage {
    std::unique_ptr<uint16_t[]> pixels; ///< RGBA F16 (half-float, 16-bit) data
    int width = 0;                      ///< Image width in pixels
    int height = 0;                     ///< Image height in pixels
    int rowBytes = 0;                   ///< Bytes per row (stride)
};

/**
 * Container for HDR image data including SDR base image and gain map
 * Supports gain map-based HDR display as per JPEG XT standard
 */
struct LoadedImageTriple {
    std::shared_ptr<LoadedImage> sdr = std::make_shared<LoadedImage>();     ///< SDR base image
    std::shared_ptr<LoadedImage> gainMap = std::make_shared<LoadedImage>(); ///< Gain map for HDR reconstruction
    bool hasGainMap = false;                                                ///< Whether gain map is available

    float minGain = 0.f;  ///< Minimum gain value from gain map
    float maxGain = 4.f;  ///< Maximum gain value from gain map
    float gamma = 1.f;    ///< Gamma correction value

    /**
     * Calculate total memory usage of all images
     * @return Total size in bytes
     */
    size_t sizeInBytes() const;
    
    /**
     * Create HDR image by applying gain map to SDR image
     * @return Shared pointer to HDR image
     */
    std::shared_ptr<LoadedImage> createHDR() const;
};

// Use DirectX implementations for half-float conversions
inline float halfToFloat(uint16_t value) {
    return DirectX::PackedVector::XMConvertHalfToFloat(value);
}

inline uint16_t floatToHalf(float value) {
    return DirectX::PackedVector::XMConvertFloatToHalf(value);
}

/**
 * Fast power function using exp/log for better performance
 * @param x Base value
 * @param gamma Exponent
 * @return x raised to the power of gamma
 */
inline float fast_powf(float x, float gamma) {
    // Use exp(gamma * log(x)), but with std::logf and std::expf (faster than powf)
    // For even more speed, you can use a polynomial/log2/exp2 approximation if gamma is fixed.
    return std::expf(gamma * std::logf(x));
}

/**
 * Fast 2^x function
 * @param G Exponent
 * @return 2 raised to the power of G
 */
inline float fast_exp2f(float G) {
    return std::exp2f(G);
}

/**
 * Fast 2^x function using bit manipulation (approximate)
 * @param G Exponent
 * @return Approximate 2^G
 */
inline float fast_exp2f_bit(float G) {
    union { uint32_t i; float f; } v;
    v.i = (uint32_t)((1 << 23) * (G + 126.94269504f));
    return v.f;
}

/**
 * Minimum of two floats
 */
inline float minf(float a, float b) {
    return a < b ? a : b;
}

/**
 * Maximum of two floats
 */
inline float maxf(float a, float b) {
    return a > b ? a : b;
}

/**
 * Convert normalized float [0,1] to 8-bit value
 * @param f Normalized float value
 * @return 8-bit value [0,255]
 */
inline uint8_t floatToByte(float f) {
    int i = static_cast<int>(f * 255.0f + 0.5f);
    return static_cast<uint8_t>(maxf(0, minf(i, 255)));
}

/**
 * Convert 8-bit value to normalized float
 * @param b 8-bit value [0,255]
 * @return Normalized float [0,1]
 */
inline float byteToFloat(uint8_t b) {
    return static_cast<float>(b) / 255.0f;
}

/**
 * Convert 8-bit value to half-float
 * @param b 8-bit value [0,255]
 * @return Half-float value
 */
inline uint16_t byteToHalf(uint8_t b) {
    const float f = byteToFloat(b);
    return floatToHalf(f);
}

/**
 * Decode gamma-encoded Display P3 to linear RGB
 * @param value Gamma-encoded value
 * @return Linear RGB value
 */
inline float decodeGamma(float value) {
    if (value <= 0.04045f) {
        return value / 12.92f;
    } else {
        return std::pow((value + 0.055f) / 1.055f, 2.4f);
    }
}

/**
 * Gamma encoding function (sRGB transfer function)
 * @param linear Linear RGB value
 * @return Gamma-encoded value
 */
inline float encodeGamma(float linear) {
    if (linear <= 0.0031308f)
        return 12.92f * linear;
    else
        return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

/**
 * Convert linear RGB (Display P3) to linear Rec. 2020
 * @param p3_r Display P3 red component
 * @param p3_g Display P3 green component
 * @param p3_b Display P3 blue component
 * @param rec2020_r Output Rec. 2020 red component
 * @param rec2020_g Output Rec. 2020 green component
 * @param rec2020_b Output Rec. 2020 blue component
 */
inline void displayP3ToLinearRec2020(float p3_r, float p3_g, float p3_b, float& rec2020_r, float& rec2020_g, float& rec2020_b) {
    // Decode gamma-encoded Display P3 values
    float linear_r = decodeGamma(p3_r);
    float linear_g = decodeGamma(p3_g);
    float linear_b = decodeGamma(p3_b);

    // Convert from Display P3 to Rec. 2020
    // The conversion matrix from Display P3 to Rec. 2020 is as follows:
    // [ R' ]   [ 0.6369580  0.1446163  0.1688800 ] [ R ]
    // [ G' ] = [ 0.2627002  0.6779980  0.0593012 ] [ G ]
    // [ B' ]   [ 0.0000000  0.0280727  1.0609850 ] [ B ]

    rec2020_r = linear_r * 0.6369580f + linear_g * 0.1446163f + linear_b * 0.1688800f;
    rec2020_g = linear_r * 0.2627002f + linear_g * 0.6779980f + linear_b * 0.0593012f;
    rec2020_b = linear_r * 0.0000000f + linear_g * 0.0280727f + linear_b * 1.0609850f;
}

const int LUT_SIZE = 256; ///< Size of the lookup table

// Lookup table for linear Rec. 2020 values
static std::vector<std::vector<float>> lut(LUT_SIZE, std::vector<float>(3));

/**
 * Precompute lookup table for Display P3 to Rec. 2020 conversion
 * This should be called once at startup for performance
 */
inline void precomputeLUT() {
    for (int i = 0; i < LUT_SIZE; ++i) {
        float p3_r = i / static_cast<float>(LUT_SIZE - 1);
        float p3_g = i / static_cast<float>(LUT_SIZE - 1);
        float p3_b = i / static_cast<float>(LUT_SIZE - 1);

        // Decode gamma-encoded Display P3 values
        float linear_r = decodeGamma(p3_r);
        float linear_g = decodeGamma(p3_g);
        float linear_b = decodeGamma(p3_b);

        // Convert to linear Rec. 2020
        lut[i][0] = linear_r * 0.6369580f + linear_g * 0.1446163f + linear_b * 0.1688800f; // R'
        lut[i][1] = linear_r * 0.2627002f + linear_g * 0.6779980f + linear_b * 0.0593012f; // G'
        lut[i][2] = linear_r * 0.0000000f + linear_g * 0.0280727f + linear_b * 1.0609850f; // B'
    }
}

/**
 * Convert Display P3 to Rec. 2020 using precomputed lookup table
 * @param p3_r Display P3 red component
 * @param p3_g Display P3 green component
 * @param p3_b Display P3 blue component
 * @param rec2020_r Output Rec. 2020 red component
 * @param rec2020_g Output Rec. 2020 green component
 * @param rec2020_b Output Rec. 2020 blue component
 */
inline void displayP3ToLinearRec2020UsingLUT(float p3_r, float p3_g, float p3_b, float& rec2020_r, float& rec2020_g, float& rec2020_b) {
    int index_r = static_cast<int>(p3_r * (LUT_SIZE - 1));
    int index_g = static_cast<int>(p3_g * (LUT_SIZE - 1));
    int index_b = static_cast<int>(p3_b * (LUT_SIZE - 1));

    rec2020_r = lut[index_r][0];
    rec2020_g = lut[index_g][1];
    rec2020_b = lut[index_b][2];
}

/**
 * Get single component from lookup table
 * @param p3 8-bit Display P3 value
 * @param i Component index (0=R, 1=G, 2=B)
 * @return Rec. 2020 component value
 */
inline float displayP3ToLinearRec2020UsingLUT(uint8_t p3, int i) {
    return lut[p3][i];
}
