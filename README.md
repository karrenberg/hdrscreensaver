# HDR ScreenSaver
A Windows screensaver / image viewer written in C++. This screensaver displays HDR images using WebView2. You need an HDR display and Windows set to HDR mode to make full use of this.
The app can be used as a Windows screensaver or standalone like a minimal image viewer app. I made it my default image viewer in Windows.

## Background
For years I've had the dream of using my awesome 1400 nits display to show all the information that my digital camera RAW files can capture instead of the compressed and tonemapped SDR you get by default. However, for a long time there were hardly any resources, not to speak of existing work flows or apps outside of film software which doesn't really target still images. Darktable can export HDR since a long time, but it's too much trial-and-error to be useful for this purpose since it always displays SDR and the work flow isn't optimized for HDR at all.
Finally, some time ago Adobe Lightroom added a full HDR workflow that displays HDR while editing, has some really nice features (SDR+HDR histogram, HDR visualization, SDR display preview and SDR-only adjustments) and allows to export several HDR image formats. And at least some of them can be displayed properly by Chromium-based browsers.

So I bit the bullet and subscribed (I really really REALLY never wanted to... I was still using LR6, and even bought a new camera that used DNG natively so I could keep using it...). But now I'm enjoying my years-old RAW files like I've never seen them before. There's no way back. It's transformative. Some images finally now show why I took them in the first place, they just could not be displayed properly in SDR.

However, app support is still lacking. Chromium doesn't work well as a viewer (no flipping through images). There's no screen saver I know of that can show HDR. Windows only displays wallpapers in SDR. immich only displays SDR. and so on...
So I decided I'll do it myself, with AI help for all the APIs and rendering stuff I don't know enough about.

I've chosen HDR JPEG as my current target because Chromium (pretty much the only app I found that can even display such images out of the box!) managed to display the exported JPEG exactly as Lightroom. This was not true e.g. for AVIF. Windows' own image preview displays HDR AVIFs in HDR, but completely wrong (so at least I'm not the only one messing this up...).

Beside Chromium/Chrome, the other useful image viewing app is Adobe's own GainMap Demo app, which can also display the gain map itself. It's good for validation (see the TODO list below...).
Unfortunately, it's closed source. If you work at Adobe, please convince people to make enough of it open source so it is easier to render the images properly.
I was unsuccessful going through Chromes enormous sources (manually and with AI help) to find out how they render the HDR image. They don't just use Skia, they have custom code on top. Skia in fact pretends to have gain map support but none of it is a) included in any existing builds and b) even if you build it manually with additional #defines you don't get any usable APIs.

In the end, however, managing the entire pipeline manually proved too complicated. I got close, but the images were never shown correctly.
So I gave up and asked the coding assistant to just wrap Chromium in a web app - and it worked almost on first try with WebView2, and with much broader support, e.g. HDR AVIF also works fine.

## Features
- Displays a slideshow of HDR and SDR images from a configurable folder (JPEG, PNG, WebP, GIF, BMP, SVG and others supported by the WebView2 runtime).
- Open-with / Explorer integration: The app can be launched from Explorer's "Open with..." on an image and/or be made the default app to open supported file types, effectively behaving like a minimal image viewer.
- Automatically skips unsupported image formats.
- Can toggle between HDR and SDR display with hotkeys H/S.
- Can use arrow keys to go to next/previous image.
- Can zoom into the image with mouse left click and move around with mouse wheel controls (difficult in screensaver mode which exits on mouse movement ;) ).
- All rendering and color management is done by WebView2.
- Graceful shutdown on ESC or Ctrl+C. Screensaver mode also exits after mouse movement or pressing any other key.
- Minimal launcher `.scr` for safe install/uninstall and Windows compatibility. This is to prevent having to copy skia and other DLLs into Windows/System (see TODO below).

## TODO
- Get rid of the launcher .scr now that no DLLs need to be copied anymore.
- Remove bright background gradient in HDR mode (WebView2 default, also present in Chrome et al)
- Improve image loading performance
- Display preview in screen saver settings dialog (currently not implemented).
- Implement a mode that sorts images by date
- Skipping of unsupported images should cache that information and not attempt to display any more of that type (otherwise dirs with many unsupported files may cause perceived hangs)
- Make sure skipping of unsupported images resets timeout (could cause perceived infinite hang in situation as above).

## HDR Images
The images displayed by the screensaver are typically exported from photo editors (e.g. Lightroom) as HDR-capable images. The app displays files that the WebView2 runtime can render, including SDR images.
Supported formats: JPEG, PNG, GIF, WebP, BMP, SVG, AVIF, TIFF, JXL (availability depends on the WebView2 runtime and installed codecs)
Tested with JPEG, AVIF, TIFF, and JXL. The latter two could not be displayed on my Windows 11 machine.

For HDR-specific workflows the original project focused on HDR JPEG exported from Lightroom in Display P3 color space as described here:
https://helpx.adobe.com/lightroom-cc/using/hdr-output.html

The files contain a base SDR image with 8 bits per pixel and a gain map to create the HDR image as described in resources/Gain_Map_1_0d15.pdf and here:
https://helpx.adobe.com/camera-raw/using/gain-map.html

The JPEGs adhere to Multi-Picture Format where the gain map is a secondary image following the main image.

XMP Specification:
https://developer.adobe.com/xmp/docs/XMPSpecifications/
https://www.adobe.com/devnet/xmp.html

XMP Toolkit:
https://github.com/adobe/XMP-Toolkit-SDK/

## Project Structure
- `src/` - Source code
- `include/` - Header files
- `resources/` - Resource files (e.g., images, icons)
- `third-party/` - External dependencies (e.g. WebView2)
- `CMakeLists.txt` - Build configuration
- `README.md` - Project documentation

## Build Instructions

### Prerequisites
- Windows 11
- CMake (https://cmake.org/)
- Visual Studio Build Tools 2022 (or newer) with C++ Desktop Development tools
- WebView2 (see below for instructions)

### WebView2
- Download WebView2 (current link: https://developer.microsoft.com/en-us/microsoft-edge/webview2)
- Unpack folder (e.g. 7zip can read the archive directly)
- Copy folder to third-party/
- Adjust path in CMakeLists.txt
- Tested with version 1.0.3485.44.

### Steps
1. Open a terminal (PowerShell or Command Prompt or git bash) in the project root directory or use the VSCode Terminal (recommended).
2. Run the following commands:

```powershell
# Create the build directory
mkdir build

# Configure the project for Visual Studio 2022 (see below for Skia vcpkg support)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build the project
cmake --build build --config Release
```

3. The resulting `.scr` file will be in `build/Release/`. Use the provided install script to register as a screensaver: `install_screensaver.bat install`.
4. To uninstall the screensaver, use `install_screensaver.bat uninstall`.

#### Notes for Visual Studio Code Users
- You do **not** need the full Visual Studio IDE, only the Build Tools 2022 with C++ support.
- You can use the CMake Tools extension for VS Code for an integrated experience.

## Usage

### Command Line Modes
- `/c` or `/c:parent_hwnd` - Configuration dialog
- `/p` or `/p:parent_hwnd` - Preview mode (for Windows screensaver preview, not implemented)
- `/s` - Screensaver mode (activated by Windows, exits on mouse movement or any key except special hotkeys)
- `/x` - Standalone mode (for testing, only exits on ESC key)
- Pass an image path as first argument to start image viewer mode (standalone without auto-advance)

**Note:** Running the screensaver without arguments will display a help message with all available options.

### Command Line Options
- `/r` - Enable random order (overrides registry setting)
  - Example: `HDRScreenSaver.scr /x /r` (standalone mode with random order enabled)
  - Example: `HDRScreenSaver.scr /s /r` (screensaver mode with random order enabled)

### Image Display
- The screensaver displays images from the configured folder.
- Images are loaded and displayed with WebView2, no custom color management or anything else.

### Special Hotkeys (work in all modes)
- **ESC** - Exit screensaver
- **Left Arrow** - Previous image
- **Right Arrow** - Next image
- **H/S** - Toggle between HDR and SDR display (if image has HDR version)

## Configuration Dialog

- The screensaver supports a configuration dialog (`/c` mode) to select the folder containing your HDR images.
- The default folder is your Windows "Pictures" folder.
- The selected folder is saved in the registry and used for all modes (screensaver, preview, standalone).
- You can change the folder at any time by running the configuration dialog again.

### Settings Options

- **Image folder**: Select the folder containing your HDR JPEG images
- **Display seconds**: How long each image is displayed before advancing to the next
- **Include subfolders**: When enabled (default), images from all subfolders of the selected folder will be included in the slideshow
- **Randomize order**: When enabled, images are displayed in random order instead of sequentially
  - Right arrow and automatic advancement select random images
  - Left arrow navigates back through history (last 1000 images viewed)
- **Enable logging**: Toggle logging to file
- **Log file path**: Location of the log file

## Creating an installer (Inno Setup)

1. Build release binaries
2. Ensure Inno Setup is installed (ISCC.exe available) or edit `installer\create_installer.bat` to point to your ISCC path.
3. From the repo root run: installer\create_installer.bat
4. The produced installer is placed under `installer\Output\`

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contributing

We welcome contributions! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on how to contribute to this project.

---

_Last updated: October 31, 2025_