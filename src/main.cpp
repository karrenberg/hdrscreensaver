// HDRScreenSaver - Main entry point and core logic
// Handles command-line parsing, screensaver/preview/standalone modes, and main slideshow loop.
// Uses Skia for image loading and color management, DirectX 11 for HDR output.

#define NOMINMAX

// Windows and DirectX includes
#include <windows.h>
#include <gdiplus.h>
#include <shlobj.h> // For SHGetKnownFolderPath, FOLDERID_Pictures

// Project includes
#include "DirectX10BitBackend.h"
#include "LoadedImageTypes.h"
#include "SkiaImageLoader.h"
#include "GdiPlusImageLoader.h"
#include "Logger.h"
#include "SettingsDialog.h"
#include "ImageCache.h"

// Standard library includes
#include <string>
#include <vector>
#include <memory>
#include <future>
#include <mutex>
#include <unordered_map>
#include <random>
#include <algorithm>
#include <filesystem>
#include <chrono>

// Signal handling
#include <csignal>

#define USE_SKIA

// Disable preview mode to prevent zombie processes from Windows screensaver settings dialog
// Comment out this line to enable preview mode (may cause zombie processes in Windows settings dialog)
#define DISABLE_PREVIEW_MODE

// --- Configuration ---
const int kCachePrev = 3;
const int kCacheNext = 4;

// --- Global shutdown flag ---
volatile bool g_shutdownRequested = false;

// --- Signal handler for Ctrl+C ---
static void SignalHandler(int signal) {
    if (signal == SIGINT) {
        LOG_MSG(L"Ctrl+C received. Shutting down gracefully...");
        g_shutdownRequested = true;
    }
}

/**
 * Get all JPEG files in a folder (case-insensitive)
 * @param folder Path to the folder to search
 * @param includeSubfolders Whether to search subdirectories recursively
 * @return Vector of JPEG file paths, sorted alphabetically
 */
static std::vector<std::wstring> GetJpegFilesInFolder(const std::wstring& folder, bool includeSubfolders = false) {
    std::vector<std::wstring> files;

    if (includeSubfolders) {
        // Recursively search through all subdirectories
        for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
            if (!entry.is_regular_file()) continue;
            auto path = entry.path();
            auto ext = path.extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (ext == L".jpg" || ext == L".jpeg") {
                files.push_back(path.wstring());
            }
        }
    } else {
        // Only search the specified folder (no subdirectories)
        for (const auto& entry : std::filesystem::directory_iterator(folder)) {
            if (!entry.is_regular_file()) continue;
            auto path = entry.path();
            auto ext = path.extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (ext == L".jpg" || ext == L".jpeg") {
                files.push_back(path.wstring());
            }
        }
    }

    // Sort files alphabetically
    std::sort(files.begin(), files.end());
    return files;
}

// ImageCache and SimpleImageLoader classes are now defined in ImageCache.h and implemented in ImageCache.cpp

/**
 * Show preview mode - displays first image in SDR mode for Windows screensaver settings
 * @param previewParent Parent window handle for the preview
 * @param settings Screensaver settings
 * @return 0 on success, 1 on failure
 */
static int showPreview(HWND previewParent, const ScreenSaverSettings& settings) {
    if (!std::filesystem::exists(settings.imageFolder)) {
        MessageBoxW(nullptr, (L"HDRScreenSaver: Image folder not found:\n" + settings.imageFolder).c_str(), L"HDRScreenSaver", MB_OK);
        return 1;
    }

    // Get preview window size
    RECT rc;
    if (!GetClientRect(previewParent, &rc)) {
        LOG_MSG(L"Failed to get preview window rect.");
        return 1;
    }

    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    LOG_MSG(L"[DEBUG] Preview window rect: left=" + std::to_wstring(rc.left) + L", top=" + std::to_wstring(rc.top) + L", right=" + std::to_wstring(rc.right) + L", bottom=" + std::to_wstring(rc.bottom));
    if (width <= 0 || height <= 0) {
        LOG_MSG(L"Preview window has invalid size.");
        return 1;
    }
    LOG_MSG(L"Preview window size: " + std::to_wstring(width) + L"x" + std::to_wstring(height));

    // --- Show first image in folder as SDR ---
    const std::vector<std::wstring> imageFiles = GetJpegFilesInFolder(settings.imageFolder, settings.includeSubfolders);
    if (imageFiles.empty()) {
        LOG_MSG(L"No .jpg images found in folder: " + settings.imageFolder);
        return 1;
    }

    LOG_MSG(L"Preview: Loading first image for preview: " + imageFiles[0]);
#ifdef USE_SKIA
    const LoadedImageTriple triple = LoadImageWithSkia(imageFiles[0]);
#else
    const LoadedImageTriple triple = LoadImageWithGdiPlus(imageFiles[0]);
#endif

    if (!triple.sdr->pixels) {
        LOG_MSG(L"Failed to load image for preview.");
        return 1;
    }

    // Initialize DirectX backend with previewParent HWND
    DirectX10BitBackend dxBackend;
    if (!dxBackend.InitializeWindowAndDevice(previewParent, width, height, true /*forceSDR*/)) {
        LOG_MSG(L"Failed to initialize DX backend for preview window.");
        return 1;
    }
    dxBackend.UploadImageBuffer(triple.sdr->pixels.get(), triple.sdr->width, triple.sdr->height, triple.sdr->rowBytes);

    // --- Test pattern for debugging (disabled) ---
    //const TestPattern pattern = GenerateTestPattern(true, width, height);
    //dxBackend.UploadImageBuffer(pattern.pixels.get(), pattern.width, pattern.height, pattern.rowBytes);

    // Message loop for preview window
    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessage(&msg, previewParent, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT || msg.message == WM_CLOSE) {
                running = false;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        dxBackend.Present();
        Sleep(16); // ~60 FPS
    }
    return 0;
}

/**
 * Show screensaver mode - main slideshow with HDR support and user interaction
 * @param shutdownOnAnyUnhandledInput Whether to exit on any unhandled input (mouse/keyboard)
 * @param settings Screensaver settings
 * @return 0 on success, 1 on failure
 */
static int showScreenSaver(const bool shutdownOnAnyUnhandledInput, const ScreenSaverSettings& settings) {
    if (!std::filesystem::exists(settings.imageFolder)) {
        MessageBoxW(nullptr, (L"HDRScreenSaver: Image folder not found:\n" + settings.imageFolder).c_str(), L"HDRScreenSaver", MB_OK);
        return 1;
    }

    // Use the image index as the cache key and loader input
    const std::vector<std::wstring> imageFiles = GetJpegFilesInFolder(settings.imageFolder, settings.includeSubfolders);
    if (imageFiles.empty()) {
        LOG_MSG(L"No .jpg images found in folder: " + settings.imageFolder);
        return 1;
    }
    LOG_MSG(L"Found " + std::to_wstring(imageFiles.size()) + L" images in folder: " + settings.imageFolder);

    DisplayMode displayMode = DisplayMode::HDR;
    DisplayMode lastNonGainMapMode = DisplayMode::SDR;

    // Initialize either cache or simple loader based on settings
    std::unique_ptr<ImageCache> cache;
    std::unique_ptr<SimpleImageLoader> simpleLoader;

    if (settings.enableCaching) {
        cache = std::make_unique<ImageCache>((size_t)settings.maxCacheMB * 1024 * 1024, kCachePrev, kCacheNext, imageFiles);
        LOG_MSG(L"Image caching enabled - using asynchronous loading with " + std::to_wstring(settings.maxCacheMB) + L" MB cache");
    } else {
        simpleLoader = std::make_unique<SimpleImageLoader>(imageFiles);
        LOG_MSG(L"Image caching disabled - using synchronous loading");
    }

    // The index of the image to display.
    size_t currentIndex = 0;

    // History tracking for backward navigation (when random mode is enabled)
    std::vector<size_t> history;
    size_t historyPosition = 0; // Current position in history
    const size_t maxHistorySize = 1000;

    // Random number generator for random mode
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, imageFiles.size() - 1);

    // Helper function to get next random index and update history
    auto getNextRandomIndex = [&]() -> size_t {
        size_t nextIndex = dist(gen);

        // Add current index to history if we're not at the beginning
        if (historyPosition < history.size()) {
            // We're in the middle of history, truncate it
            history.resize(historyPosition);
        }

        // Add current index to history
        history.push_back(currentIndex);
        if (history.size() > maxHistorySize) {
            history.erase(history.begin());
        }
        historyPosition = history.size();

        return nextIndex;
    };

    // Load the first image
    std::shared_ptr<LoadedImage> currentImg;
    bool hasGainMap = true;
    {
        std::shared_ptr<LoadedImageTriple> triplePtr;
        if (settings.enableCaching) {
            const auto fut = cache->get_async(currentIndex);
            triplePtr = fut.get();
        } else {
            triplePtr = simpleLoader->load_sync(currentIndex);
        }

        if (!triplePtr) {
            LOG_MSG(L"Failed to load initial image");
            return 1;
        }

        hasGainMap = triplePtr->hasGainMap;
        if (hasGainMap) {
            switch (displayMode) {
                case DisplayMode::HDR: currentImg = triplePtr->createHDR(); break;
                case DisplayMode::SDR: currentImg = triplePtr->sdr; break;
                case DisplayMode::GainMap: currentImg = triplePtr->gainMap; break;
                default: currentImg = nullptr; break;
            }
        } else {
            currentImg = triplePtr->sdr;
        }
    }

    if (!currentImg)
    {
        LOG_MSG(L"Failed to load initial image: " + std::to_wstring(reinterpret_cast<uintptr_t>(currentImg.get())));
        return 1;
    }

    // --- DX11 HDR Backend ---
    DirectX10BitBackend dxBackend;
    if (!dxBackend.InitializeWindowAndDevice()) {
        LOG_MSG(L"Failed to initialize DX backend window/device.");
        return 1;
    }

    dxBackend.UploadImageBuffer(currentImg->pixels.get(), currentImg->width, currentImg->height, currentImg->rowBytes);
    dxBackend.Present();
    bool running = true;
    HWND hwnd = dxBackend.GetWindowHandle();

    // --- Mouse movement detection: record initial mouse position ---
    POINT initialMousePos = {0, 0};
    GetCursorPos(&initialMousePos);
    bool mouseMoved = false;

    // --- Fill the cache ---
    // Seems to be unnecessary after optimizations.
    //cache.fill(imageFiles, currentIndex, true);

    // --- Display images in a slideshow ---
    while (running && !g_shutdownRequested) {
        size_t lastIndex = currentIndex;
        DisplayMode lastMode = displayMode;
        const auto start = std::chrono::steady_clock::now();

        while (true) {
            MSG msg;
            while (PeekMessage(&msg, hwnd, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT || msg.message == WM_CLOSE) {
                    running = false;
                    g_shutdownRequested = true;
                }

                // Handle mouse movement - exit screensaver only if mouse actually moved
                if (msg.message == WM_MOUSEMOVE && shutdownOnAnyUnhandledInput) {
                    POINT pt;
                    GetCursorPos(&pt);
                    if (!mouseMoved && (pt.x != initialMousePos.x || pt.y != initialMousePos.y)) {
                        LOG_MSG(L"Mouse movement detected - exiting screensaver");
                        running = false;
                        g_shutdownRequested = true;
                        PostMessage(hwnd, WM_CLOSE, 0, 0);
                        mouseMoved = true;
                        break;
                    }
                }
                // Debug: log all mouse messages (only in screensaver mode)
                if (msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST && shutdownOnAnyUnhandledInput) {
                    LOG_MSG(L"Mouse message received: " + std::to_wstring(msg.message));
                }

                if (msg.message == WM_KEYDOWN) {
                    if (msg.wParam == VK_ESCAPE) {
                        running = false;
                        g_shutdownRequested = true;
                        PostMessage(hwnd, WM_CLOSE, 0, 0); // Ensure window is closed
                    } else if (msg.wParam == VK_RIGHT) {
                        if (settings.randomizeOrder) {
                            currentIndex = getNextRandomIndex();
                        } else {
                            currentIndex = (currentIndex + 1) % imageFiles.size();
                        }
                        if (settings.enableCaching) {
                            cache->set_current_index(currentIndex);
                        }
                    } else if (msg.wParam == VK_LEFT) {
                        if (settings.randomizeOrder) {
                            // Go back in history
                            if (historyPosition > 0) {
                                historyPosition--;
                                currentIndex = history[historyPosition];
                            }
                        } else {
                            currentIndex = (currentIndex + imageFiles.size() - 1) % imageFiles.size();
                        }
                        if (settings.enableCaching) {
                            cache->set_current_index(currentIndex);
                        }
                    } else if (msg.wParam == 'H' || msg.wParam == 'h' || msg.wParam == 'S' || msg.wParam == 's') {
                        // Do nothing if we only have an SDR image.
                        if (hasGainMap) {
                            if (displayMode == DisplayMode::HDR) displayMode = DisplayMode::SDR;
                            else if (displayMode == DisplayMode::SDR) displayMode = DisplayMode::HDR;
                            else if (displayMode == DisplayMode::GainMap) displayMode = lastNonGainMapMode;
                            if (displayMode != DisplayMode::GainMap) lastNonGainMapMode = displayMode;
                        }
                    } else if (msg.wParam == 'G' || msg.wParam == 'g') {
                        // Do nothing if we only have an SDR image.
                        if (hasGainMap) {
                            if (displayMode == DisplayMode::GainMap) displayMode = lastNonGainMapMode;
                            else { lastNonGainMapMode = displayMode; displayMode = DisplayMode::GainMap; }
                        }
                    } else if (shutdownOnAnyUnhandledInput) {
                        // Any other key exits the screensaver
                        LOG_MSG(L"Non-special key pressed - exiting screensaver");
                        running = false;
                        g_shutdownRequested = true;
                        PostMessage(hwnd, WM_CLOSE, 0, 0);
                        break;
                    }
                }

                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            if (!running || g_shutdownRequested) {
                PostMessage(hwnd, WM_CLOSE, 0, 0); // Ensure window is closed if not already
                break;
            }

            if (currentIndex != lastIndex || displayMode != lastMode)
                break;

            if ((std::chrono::steady_clock::now() - start) > std::chrono::seconds(settings.displaySeconds)) {
                if (settings.randomizeOrder) {
                    currentIndex = getNextRandomIndex();
                } else {
                    currentIndex = (currentIndex + 1) % imageFiles.size();
                }
                if (settings.enableCaching) {
                    cache->set_current_index(currentIndex);
                }
                break;
            }

            Sleep(10);
        }

        // --- Smart Preloading: After advancing currentIndex, preload next image(s) in the same direction ---
        if (settings.enableCaching) {
            const bool isForward = (currentIndex >= lastIndex);
            bool preloaded = false;
            const size_t numImages = imageFiles.size();
            if (isForward) { // Forward or initial
                for (int i = 1; i <= kCacheNext; ++i) {
                    size_t probe = (currentIndex + i) % numImages;
                    if (!cache->is_loaded(probe) && !cache->is_in_cache_or_inflight(probe)) {
                        size_t estimate = cache->get_estimated_image_size();
                        if (!cache->would_be_evicted_public(probe, estimate, true)) {
                            cache->get_async(probe, true);
                            LOG_MSG(L"Preloading image " + std::to_wstring(probe+1) + L"/" + std::to_wstring(numImages) + L": " + imageFiles[probe]);
                            preloaded = true;
                            break;
                        }
                    }
                }

                // If nothing was preloaded, force preload the first not-yet-loaded image in the forward direction (no eviction check)
                if (!preloaded) {
                    for (size_t offset = 1; offset < numImages; ++offset) {
                        size_t probe = (currentIndex + offset) % numImages;
                        if (!cache->is_loaded(probe) && !cache->is_in_cache_or_inflight(probe)) {
                            cache->get_async(probe, true);
                            LOG_MSG(L"Forced preloading image (no eviction check) " + std::to_wstring(probe+1) + L"/" + std::to_wstring(numImages) + L": " + imageFiles[probe]);
                            preloaded = true;
                            break;
                        }
                    }
                }
            } else { // Backward
                for (int i = 1; i <= kCachePrev; ++i) {
                    size_t probe = (currentIndex + numImages - i) % numImages;
                    if (!cache->is_loaded(probe) && !cache->is_in_cache_or_inflight(probe)) {
                        size_t estimate = cache->get_estimated_image_size();
                        if (!cache->would_be_evicted_public(probe, estimate, false)) {
                            cache->get_async(probe, false);
                            LOG_MSG(L"Preloading image " + std::to_wstring(probe+1) + L"/" + std::to_wstring(numImages) + L": " + imageFiles[probe]);
                            preloaded = true;
                            break;
                        }
                    }
                }

                // If nothing was preloaded, force preload the first not-yet-loaded image in the backward direction (no eviction check)
                if (!preloaded) {
                    size_t probe = (currentIndex + numImages - 1) % numImages;
                    size_t start = probe;
                    do {
                        if (!cache->is_loaded(probe) && !cache->is_in_cache_or_inflight(probe)) {
                            cache->get_async(probe, false);
                            LOG_MSG(L"Forced preloading image (no eviction check) " + std::to_wstring(probe+1) + L"/" + std::to_wstring(numImages) + L": " + imageFiles[probe]);
                            preloaded = true;
                            break;
                        }
                        probe = (probe + numImages - 1) % numImages;
                    } while (probe != start);
                }
            }

            if (!preloaded) {
                LOG_MSG(L"No suitable image to preload after currentIndex " + std::to_wstring(currentIndex));
            }
        }

        if (!running || g_shutdownRequested)
            break;

        // Load and display the current image
        std::shared_ptr<LoadedImageTriple> triplePtr;
        if (settings.enableCaching) {
            // Asynchronous loading when caching is enabled
            auto fut = cache->get_async(currentIndex);
            if (fut.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                triplePtr = fut.get();
            } else {
                LOG_MSG(L"Still waiting for image " + std::to_wstring(currentIndex+1) + L"/" + std::to_wstring(imageFiles.size()) + L": " + imageFiles[currentIndex]);
                continue;
            }
        } else {
            // Synchronous loading when caching is disabled
            triplePtr = simpleLoader->load_sync(currentIndex);
        }

        if (triplePtr) {
            std::shared_ptr<LoadedImage> cached = nullptr;
            if (triplePtr->hasGainMap) {
                switch (displayMode) {
                    case DisplayMode::HDR: cached = triplePtr->createHDR(); break;
                    case DisplayMode::SDR: cached = triplePtr->sdr; break;
                    case DisplayMode::GainMap: cached = triplePtr->gainMap; break;
                    default: cached = nullptr; break;
                }
            } else {
                cached = triplePtr->sdr;
            }

            if (cached && cached->pixels) {
                dxBackend.UploadImageBuffer(cached->pixels.get(), cached->width, cached->height, cached->rowBytes);
                if (!running || g_shutdownRequested) break;
                dxBackend.Present();
                LOG_MSG(L"Displaying image " + std::to_wstring(currentIndex+1) + L"/" + std::to_wstring(imageFiles.size()) + L" [" + (displayMode==DisplayMode::HDR?L"HDR":displayMode==DisplayMode::SDR?L"SDR":L"GainMap") + L"]: " + imageFiles[currentIndex]);
            } else {
                LOG_MSG(L"Failed to load image " + std::to_wstring(currentIndex+1) + L"/" + std::to_wstring(imageFiles.size()) + L": " + imageFiles[currentIndex]);
            }
        } else {
            LOG_MSG(L"Failed to load image " + std::to_wstring(currentIndex+1) + L"/" + std::to_wstring(imageFiles.size()) + L": " + imageFiles[currentIndex]);
        }
    }

    LOG_MSG(L"Exiting slideshow.");
    return 0;
}

// --- Registry helpers for image folder config ---

/**
 * Get the default Pictures folder path
 * @return Path to the user's Pictures folder
 */
static std::wstring GetDefaultPicturesFolder() {
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &path))) {
        result = path;
        CoTaskMemFree(path);
    }
    return result;
}

/**
 * Load the image folder path from registry
 * @return Image folder path from registry, or default Pictures folder if not found
 */
static std::wstring LoadImageFolderFromRegistry() {
    HKEY hKey;
    wchar_t buf[MAX_PATH] = {0};
    DWORD len = sizeof(buf);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\HDRScreenSaver", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"ImageFolder", nullptr, nullptr, (LPBYTE)buf, &len) == ERROR_SUCCESS && wcslen(buf) > 0) {
            RegCloseKey(hKey);
            return buf;
        }
        RegCloseKey(hKey);
    }
    return GetDefaultPicturesFolder();
}

/**
 * Save the image folder path to registry
 * @param folder Path to save to registry
 */
static void SaveImageFolderToRegistry(const std::wstring& folder) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\HDRScreenSaver", 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"ImageFolder", 0, REG_SZ, (const BYTE*)folder.c_str(), (DWORD)((folder.size()+1)*sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
}


/**
 * Convert LPSTR (char*) to std::wstring
 * @param str Null-terminated C string to convert
 * @return Wide string representation
 */
static std::wstring ToWString(const char* str) {
    if (!str) return L"";
    size_t len = strlen(str);
    std::wstring wstr(len, L' ');
    mbstowcs(&wstr[0], str, len);
    return wstr;
}

/**
 * Show comprehensive help message
 */
static void ShowHelpMessage() {
    std::wstring helpMessage = L"HDRScreenSaver - HDR Image Slideshow Screensaver\n\n";
    helpMessage += L"Usage:\n";
    helpMessage += L"  HDRScreenSaver.scr /c          - Configuration dialog\n";
    helpMessage += L"  HDRScreenSaver.scr /p[:hwnd]   - Preview mode (for Windows settings)\n";
    helpMessage += L"  HDRScreenSaver.scr /s          - Screensaver mode (activated by Windows)\n";
    helpMessage += L"  HDRScreenSaver.scr /x          - Standalone mode (for testing)\n\n";
    helpMessage += L"Options:\n";
    helpMessage += L"  /preload                       - Enable image caching\n";
    helpMessage += L"  /r                             - Enable random order\n";
    helpMessage += L"  /f <path>                      - Override image folder path\n\n";
    helpMessage += L"Examples:\n";
    helpMessage += L"  HDRScreenSaver.scr /x          - Run in standalone mode\n";
    helpMessage += L"  HDRScreenSaver.scr /x /preload - Run standalone with caching\n";
    helpMessage += L"  HDRScreenSaver.scr /s /r       - Run screensaver with random order\n";
    helpMessage += L"  HDRScreenSaver.scr /x /f \"C:\\Photos\" - Run with custom folder\n\n";
    helpMessage += L"For more information, see the README.md file.";
    MessageBoxW(nullptr, helpMessage.c_str(), L"HDRScreenSaver - Help", MB_OK | MB_ICONINFORMATION);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int x) {
    // Register signal handler for Ctrl+C
    std::signal(SIGINT, SignalHandler);
    LOG_MSG(L"HDRScreenSaver starting. Command line: '", ToWString(lpCmdLine), L"'");

    // Initialize COM so we can use WIC (Windows Imaging Component) for color space conversions.
    CoInitialize(NULL);

    // Use CommandLineToArgvW for robust argument parsing
    wchar_t mode;
    std::wstring param;
    bool enableCachingOverride = false;
    bool randomizeOrderOverride = false;
    std::wstring imageFolderOverride;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv || argc < 2) {
            ShowHelpMessage();
            if (argv) LocalFree(argv);
            return 0;
        }

        std::wstring arg1 = argv[1];
        if (arg1.size() > 0 && (arg1[0] == L'/' || arg1[0] == L'-'))
            arg1 = arg1.substr(1);
        mode = towlower(arg1[0]);
        param = arg1.size() > 2 && arg1[1] == L':' ? arg1.substr(2) : L"";
        if (param.empty() && argc > 2)
            param = argv[2];

        // Check for help flags first
        bool helpRequested = false;
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"-h" || arg == L"--help" || arg == L"-help" || arg == L"/h" || arg == L"/help") {
                helpRequested = true;
                break;
            }
        }

        if (helpRequested) {
            ShowHelpMessage();
            LocalFree(argv);
            return 0;
        }

        // Check for additional command line flags
        for (int i = 2; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"-preload" || arg == L"/preload") {
                enableCachingOverride = true;
                LOG_MSG(L"Command line flag -preload detected: enabling image caching");
            } else if (arg == L"-r" || arg == L"/r") {
                randomizeOrderOverride = true;
                LOG_MSG(L"Command line flag -r detected: enabling random order");
            } else if (arg.substr(0, 2) == L"-f" || arg.substr(0, 2) == L"/f") {
                // Image folder override: -f "path" or /f "path"
                if (arg.length() > 2 && arg[2] == L'=') {
                    // Format: -f=path or /f=path
                    imageFolderOverride = arg.substr(3);
                } else if (i + 1 < argc) {
                    // Format: -f path or /f path
                    imageFolderOverride = argv[i + 1];
                    i++; // Skip the next argument since we consumed it
                } else {
                    LOG_MSG(L"Error: -f flag requires a folder path");
                    MessageBoxW(nullptr, L"Error: -f flag requires a folder path", L"HDRScreenSaver - Error", MB_OK | MB_ICONERROR);
                    LocalFree(argv);
                    return 1;
                }
                LOG_MSG(L"Command line flag -f detected: overriding image folder to: " + imageFolderOverride);
            }
        }

        LocalFree(argv);
    }

    // Load settings from registry
    ScreenSaverSettings settings = LoadSettingsFromRegistry();

    // Apply command line overrides
    if (enableCachingOverride) {
        settings.enableCaching = true;
        LOG_MSG(L"Command line override: caching enabled");
    }
    if (randomizeOrderOverride) {
        settings.randomizeOrder = true;
        LOG_MSG(L"Command line override: random order enabled");
    }
    if (!imageFolderOverride.empty()) {
        settings.imageFolder = imageFolderOverride;
        LOG_MSG(L"Command line override: image folder set to: " + settings.imageFolder);
    }

    Logger::Instance().Configure(settings.logEnabled, settings.logPath);

    // --- Determine mode: screensaver, preview, or standalone ---
    switch (mode) {
        case L'c': {
            LOG_MSG(L"Configuration mode requested.");
            ScreenSaverSettings settings = LoadSettingsFromRegistry();
            if (ShowSettingsDialog(nullptr, settings)) {
                SaveSettingsToRegistry(settings);
                MessageBoxW(nullptr, L"Settings saved.", L"HDRScreenSaver", MB_OK);
                LOG_MSG(L"Settings updated and saved.");
            } else {
                LOG_MSG(L"Settings dialog cancelled or unchanged.");
            }
            return 0;
        }

        case L'p': {
#ifdef DISABLE_PREVIEW_MODE
            LOG_MSG(L"Preview mode disabled by DISABLE_PREVIEW_MODE flag");
            return 0;
#else
            LOG_MSG(L"Preview mode requested.");
            HWND previewParent = nullptr;

            if (!param.empty()) {
                previewParent = (HWND)_wtoi64(param.c_str());
                LOG_MSG(L"Preview parent HWND: " + std::to_wstring(reinterpret_cast<uintptr_t>(previewParent)));
            }

            if (!previewParent) {
                LOG_MSG(L"No preview parent HWND: " + std::to_wstring(reinterpret_cast<uintptr_t>(previewParent)));
                return 1;
            }

            return showPreview(previewParent, settings);
#endif
        }

        case L's': {
            LOG_MSG(L"Screensaver mode requested.");
            return showScreenSaver(true /*shutdownOnAnyUnhandledInput*/, settings);
        }

        case L'x': {
            LOG_MSG(L"Standalone mode requested.");
            return showScreenSaver(false /*shutdownOnAnyUnhandledInput*/, settings);
        }

        default: {
            // Show help message for invalid modes
            LOG_MSG(L"Unknown mode: '" + std::wstring(1, mode) + L"'. Showing help.");
            ShowHelpMessage();
            return 1;
        }
    }
}
