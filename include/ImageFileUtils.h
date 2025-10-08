#pragma once

#include <set>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

static inline bool IsImagePath(const std::filesystem::path& path)
{
    auto ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    // Common web/bitmap formats; runtime support depends on WebView2/Chromium and OS codecs

    static const std::set<std::wstring> supportedFormats = { L".jpg", L".jpeg", L".png", L".gif", L".bmp", L".webp", L".svg", L".avif", L".jxl", L".tif", L".tiff" };

    return supportedFormats.count( ext );
}

/**
 * Get all image files in a folder (case-insensitive) matching supported extensions
 * @param folder Path to the folder to search
 * @param includeSubfolders Whether to search subdirectories recursively
 * @return Vector of image file paths
 */
static inline std::vector<std::wstring> GetImageFilesInFolder(const std::wstring& folder, bool includeSubfolders = false)
{
    std::vector<std::wstring> files;
    if (includeSubfolders) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
            if (!entry.is_regular_file()) continue;
            if (IsImagePath(entry.path())) files.push_back(entry.path().wstring());
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(folder)) {
            if (!entry.is_regular_file()) continue;
            if (IsImagePath(entry.path())) files.push_back(entry.path().wstring());
        }
    }
    return files;
}
