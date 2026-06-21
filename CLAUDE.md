# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

Requires Windows 11, CMake, Visual Studio Build Tools 2022 (C++ Desktop Development), and the WebView2 SDK unpacked into `third-party/microsoft.web.webview2.1.0.3485.44/`.

The location map overlay needs an offline world map at `resources/world_map.jpg` — a ~16k×8k equirectangular image (e.g. public-domain Natural Earth). It is not checked in; if absent, the build still succeeds and the overlay simply stays inert. When present, CMake copies it next to the executable and the installer bundles it.

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
# Output: build/Release/HDRScreenSaver.scr (main app) and HDRScreenSaverLauncher.scr
```

To install/uninstall as a Windows screensaver:
```bash
install_screensaver.bat install
install_screensaver.bat uninstall
```

To create an installer (requires Inno Setup):
```bash
installer\create_installer.bat
# Output: installer\Output\HDRScreenSaver_v1.0.0_Installer.exe
```

## Running / Testing

Run directly after building (no separate test suite exists):
```
build\Release\HDRScreenSaver.scr /x            # Standalone mode
build\Release\HDRScreenSaver.scr /x /r         # Standalone with random order
build\Release\HDRScreenSaver.scr /c            # Configuration dialog
build\Release\HDRScreenSaver.scr /s            # Screensaver mode
build\Release\HDRScreenSaver.scr <image.jpg>   # Open-with / image viewer mode
```

## Architecture

The app is a Win32 C++20 application that outputs two `.scr` binaries:

**`HDRScreenSaver.scr`** — the main app, built from:
- `src/main.cpp` — `WinMain` entry point; parses command-line args (`/s`, `/x`, `/c`, `/p`, `/r`, `/f`), loads/saves settings, dispatches to the appropriate mode.
- `src/WebView2Mode.cpp` — `RunWebView2Mode()`: creates the Win32 window and embeds a WebView2 control. Handles the image slideshow loop, low-level keyboard/mouse hooks (to intercept hotkeys from WebView2's focus), HDR/SDR toggling, history-aware navigation, auto-advance timer, and driving the location map overlay.
- `src/MapOverlay.cpp` / `include/MapOverlay.h` — `MapOverlay`: a layered, click-through popup window shown in the lower-left corner. Renders either the bundled offline world map (GDI+, cropped around the GPS coordinate) or a composited satellite tile crop fetched by `TileMap.cpp`. `SetOnlineMode(bool)` switches rendering mode at runtime without restarting the worker thread. Independent of the WebView, so it floats above it without touching the image-rendering path.
- `src/TileMap.cpp` / `include/TileMap.h` — `FetchSatelliteCrop()`: fetches ESRI ArcGIS satellite+labels tiles via WinHTTP on a background worker thread, composites them into a square bitmap centred on the GPS coordinate, and caches tiles to disk under `%LOCALAPPDATA%\HDRScreenSaver\tilecache`.
- `include/ImageMetadata.h` — header-only `ReadGpsLocation()`: reads EXIF GPS latitude/longitude from an image via WIC. Fully offline.
- `src/SettingsDialog.cpp` — Win32 dialog for user configuration (`/c` mode). `ScreenSaverSettings` struct and registry persistence (`HKCU\Software\HDRScreenSaver`).
- `include/ImageFileUtils.h` — header-only helpers: `IsImagePath()` and `GetImageFilesInFolder()`.
- `include/Logger.h` — header-only thread-safe singleton logger; writes to `wcout` and optionally a log file. Use the `LOG_MSG(...)` macro.

**`HDRScreenSaverLauncher.scr`** — minimal launcher registered with Windows as the `.scr` file; simply `CreateProcess`es `C:\Program Files\HDRScreenSaver\HDRScreenSaver.exe` and waits. Statically links CRT so it has no DLL dependencies.

### Key design points

- All rendering is delegated to WebView2 (Chromium-based). The C++ host navigates the WebView directly to the image's `file://` URI (one image per navigation); there is no HTML wrapper page.
- HDR images are HDR JPEG (gain-map / MPF format as exported from Lightroom). WebView2/Chromium handles HDR display natively; the app does no custom color management.
- Hotkeys (arrow keys, ESC, H, S, R, M) are intercepted via a Win32 low-level keyboard hook (`WH_KEYBOARD_LL`) because WebView2 consumes keyboard input. The hook posts `WM_APP+1` messages to the host window's message loop. `H`/`S` toggle HDR/SDR, `R` toggles random order, `M` cycles the location map overlay.
- The **location map overlay** has a display state cycled at runtime with `M`. The available states depend on the `MapOnlineMode` setting: with online mode off, `M` cycles off → offline map → off; with online mode on, `M` cycles off → offline map → satellite → off. While satellite tiles are loading the offline map is shown as a placeholder (clamped to its minimum renderable zoom if the requested zoom is too fine). `TileMap.cpp` fetches high-quality hybrid satellite + labels tiles from keyless ESRI ArcGIS tile servers (WinHTTP/HTTPS) on a background worker thread (always running, regardless of the current display state), composites them, and hands the result back to the UI via `PostMessage`; disk tiles are cached under `%LOCALAPPDATA%\HDRScreenSaver\tilecache`. On any fetch failure it falls back to the offline map. `ReadGpsLocation()` (WIC) extracts EXIF GPS from each image. Images without GPS data show no map. `MapZoomX1000` (registry DWORD, value = degrees × 1000, range 0.002–25.0) controls the half-extent in degrees cropped around the pin; the settings dialog exposes this as a free-text field.
- Settings are persisted in the Windows registry under `HKCU\Software\HDRScreenSaver`.
- There is no test suite; verify behavior by running in `/x` (standalone) mode.
