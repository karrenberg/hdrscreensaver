#pragma once
#include <windows.h>
#include <string>

// Abstract interface for image loading backends
class IImageBackend {
public:
    virtual ~IImageBackend() = default;
    // Loads an image from the given path and returns a HBITMAP handle
    virtual HBITMAP LoadImage(const std::wstring& imagePath) = 0;
};
