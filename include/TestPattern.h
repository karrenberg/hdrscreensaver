#pragma once

#include <memory>

class LoadedImageTriple;

struct TestPattern {
    std::unique_ptr<uint16_t[]> pixels;
    int width;
    int height;
    int rowBytes;
};

// --- Utility: Generate a test pattern (SDR or HDR) as a pixel buffer (RGBA F16) ---
TestPattern GenerateTestPattern(bool hdr, int width, int height);

LoadedImageTriple createTripleFromPattern(const TestPattern& pattern);