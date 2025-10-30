// WebView2Mode.h - WebView2-based slideshow mode
#pragma once

#include <windows.h>
#include <string>

#include "SettingsDialog.h" // for ScreenSaverSettings

// Runs the slideshow using WebView2 to display images.
// - shutdownOnAnyUnhandledInput: mimic screensaver exit behavior
// - settings: reuse existing configuration (folder, randomize, seconds, etc.)
// If `singleImagePath` is provided and not empty, the app will start in image viewing mode, showing that image and allowing arrow keys to cycle.
// If `disableAutoAdvance` is true, automatic advancing based on `settings.displaySeconds` is disabled.
int RunWebView2Mode(bool shutdownOnAnyUnhandledInput, const ScreenSaverSettings& settings, const std::wstring& singleImagePath = L"", bool disableAutoAdvance = false);


