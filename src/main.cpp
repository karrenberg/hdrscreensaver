// HDRScreenSaver - Main entry point and core logic
// Handles command-line parsing, screensaver/standalone modes, and main slideshow loop.
// Uses WebView2 for image loading, color management, and display.

#define NOMINMAX

// Windows includes
#include <windows.h>
#include <shlobj.h> // For SHGetKnownFolderPath, FOLDERID_Pictures

// Project includes
#include "Logger.h"
#include "SettingsDialog.h"
#include "WebView2Mode.h"

// Standard library includes
#include <string>
#include <vector>
#include <memory>
#include <future>
#include <random>
#include <algorithm>
#include <filesystem>

// Signal handling
#include <csignal>


// --- Global shutdown flag ---
volatile bool g_shutdownRequested = false;

// --- Signal handler for Ctrl+C ---
static void SignalHandler(int signal) {
    if (signal == SIGINT) {
        LOG_MSG(L"Ctrl+C received. Shutting down gracefully...");
        g_shutdownRequested = true;
    }
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
    // Convert using the system ANSI code page to match prior behavior of mbstowcs
    int required = MultiByteToWideChar(CP_ACP, 0, str, -1, nullptr, 0);
    if (required <= 0) return L"";
    std::wstring wstr(required - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, str, -1, &wstr[0], required);
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
    helpMessage += L"  /r                             - Enable random order\n";
    helpMessage += L"  /f <path>                      - Override image folder path\n\n";
    helpMessage += L"Examples:\n";
    helpMessage += L"  HDRScreenSaver.scr /x          - Run in standalone mode\n";
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
    struct CoInit {
        CoInit() { CoInitialize(nullptr); }
        ~CoInit() { CoUninitialize(); }
    } coinit;

    // Use CommandLineToArgvW for robust argument parsing
    wchar_t mode;
    std::wstring param;
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
            if (arg == L"-r" || arg == L"/r") {
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
            LOG_MSG(L"Preview mode not implemented");
            return 0;
        }

        case L's': {
            LOG_MSG(L"Screensaver mode requested.");
            return RunWebView2Mode(true /*shutdownOnAnyUnhandledInput*/, settings);
        }

        case L'x': {
            LOG_MSG(L"Standalone mode requested.");
            return RunWebView2Mode(false /*shutdownOnAnyUnhandledInput*/, settings);
        }

        default: {
            // Show help message for invalid modes
            LOG_MSG(L"Unknown mode: '" + std::wstring(1, mode) + L"'. Showing help.");
            ShowHelpMessage();
            return 1;
        }
    }
}
