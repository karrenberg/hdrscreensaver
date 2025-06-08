// SkiaImageLoader.h
// Loads an image using Skia and returns a simple struct with pixel data and metadata.
#pragma once
#include <string>
#include "LoadedImageTypes.h"

// Loads an image from disk, converts to Rec.2020 linear RGBA F16, and returns a LoadedImageTriple.
// Returns nullptr if loading fails.
LoadedImageTriple LoadImageWithSkia(const std::wstring& imagePathW);
