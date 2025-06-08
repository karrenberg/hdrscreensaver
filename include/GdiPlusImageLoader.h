// GdiPlusImageLoader.h
// Loads an image using GDI+ and returns a simple struct with pixel data and metadata.
#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include "LoadedImageTypes.h"

// Loads an image from disk using GDI+, converts to Rec.2020 linear RGBA F16, and returns a LoadedImageTriple.
// Returns nullptr if loading fails.
LoadedImageTriple LoadImageWithGdiPlus(const std::wstring& imagePathW);
