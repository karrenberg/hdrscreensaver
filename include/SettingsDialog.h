#pragma once
#include <windows.h>
#include <string>

struct ScreenSaverSettings {
    std::wstring imageFolder;
    int displaySeconds;
    int maxCacheMB;
    bool logEnabled;
    std::wstring logPath;
    bool enableCaching;
    bool includeSubfolders;
    bool randomizeOrder;
};

// Shows the settings dialog. Returns true if settings were changed and saved.
bool ShowSettingsDialog(HWND parent, ScreenSaverSettings& settings);
// Loads settings from registry (or defaults)
ScreenSaverSettings LoadSettingsFromRegistry();
// Saves settings to registry
void SaveSettingsToRegistry(const ScreenSaverSettings& settings);
