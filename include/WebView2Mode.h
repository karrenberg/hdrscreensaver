// WebView2Mode.h - WebView2-based slideshow mode
#pragma once

#include <windows.h>
#include <string>

#include "SettingsDialog.h" // for ScreenSaverSettings

// Runs the slideshow using WebView2 to display images.
// - shutdownOnAnyUnhandledInput: mimic screensaver exit behavior
// - settings: reuse existing configuration (folder, randomize, seconds, etc.)
int RunWebView2Mode(bool shutdownOnAnyUnhandledInput, const ScreenSaverSettings& settings);


