# HDR ScreenSaver

A Windows screensaver written in C++. This screensaver displays HDR images using GDI+ or Skia for image loading and color management, and DirectX 11 for true HDR output. You need an HDR display and Windows set to HDR mode to run this.
The app can be used as a Windows screensaver or standalone (like an image viewing app).

For years I've had the dream of using my awesome 1400 nits display to show all the information that my digital camera RAW files can capture instead of the compressed and tonemapped SDR you get by default. However, for a long time there were hardly any resources, not to speak of existing work flows or apps outside of film software which doesn't really target still images. Darktable can export HDR since a long time, but it's too much trial-and-error to be useful for this purpose since it always displays SDR and the work flow isn't optimized for HDR at all.
Finally, some time ago Adobe Lightroom added a full HDR workflow that displays HDR while editing, has some really nice features (SDR+HDR histogram, HDR visualization, SDR display preview and SDR-only adjustments) and allows to export several HDR image formats. And at least some of them can be displayed properly by Chromium-based browsers.

So I bit the bullet and subscribed (I really really REALLY never wanted to... I was still using LR6, and even bought a new camera that used DNG natively so I could keep using it...). But now I'm enjoying my years-old RAW files like I've never seen them before. There's no way back. It's transformative. Some images finally now show why I took them in the first place, they just could not be displayed properly in SDR.

However, app support is still lacking. Chromium doesn't work well as a viewer (no flipping through images). There's no screen saver that can show HDR. Windows only displays wallpapers in SDR. immich only displays SDR. and so on...
So I decided I'll do it myself, with AI help for all the APIs and rendering stuff I don't know enough about.

I've chosen HDR JPEG as my current target because Chromium (pretty much the only app I found that can even display such images out of the box!) managed to display the exported JPEG exactly as Lightroom. This was not true e.g. for AVIF. Windows' own image preview displays HDR AVIFs in HDR, but completely wrong (so at least I'm not the only one messing this up...).

Beside Chromium/Chrome, the other useful image viewing app is Adobe's own GainMap Demo app, which can also display the gain map itself. It's good for validation (see the TODO list below...).
Unfortunately, it's closed source. If you work at Adobe, please convince people to make enough of it open source so it is easier to render the images properly.
I was unsuccessful going through Chromes enormous sources (manually and with AI help) to find out how they render the HDR image. They don't just use Skia, they have custom code on top. Skia in fact pretends to have gain map support but none of it is a) included in any existing builds and b) even if you build it manually with additional #defines you don't get any usable APIs.

## Features
- Displays a slideshow of HDR JPEG images from a configurable folder.
- Can toggle between HDR, SDR, and gain map display with hotkeys H/S/G.
- Can use arrow keys to go to next/previous image.
- Very basic (and most likely entirely wrong) color management: loads images with Skia or GDI+ and converts to Rec.2020 linear for gain map application and HDR output
- True HDR output: uses a 16-bit float/channel DXGI swapchain and DirectX 11 pipeline
- Graceful shutdown on ESC or Ctrl+C. Screensaver mode also exits after mouse movement or pressing any other key.
- Minimal launcher `.scr` for safe install/uninstall and Windows compatibility. This is to prevent having to copy skia and other DLLs into Windows/System
- Configurable image caching: synchronous loading by default, can enable preloading for smoother performance (via settings dialog or command line flag /preload)

## TODO
- The only supported image types currently are standard JPEG and HDR JPEG as created by Adobe Lightroom.
- The renndering does not take the actual display into account at all. Testing has been conducted with
  a 1400 nits MiniLED monitor.
- HDR colors are not correct. GDI+ is even worse than Skia.
  - The current gain map application artificially boosts the maxGain by 1.f, which brings us closer
    to how Adobe GainMap Demo app and Chrome render images. However, there are still noticeable and
    strong differences, e.g. a sunset missing reds, a tree not saturated enough in its yellow-green HDR
    highlights, beach sand that has a slightly greenish tint, ...
  - The images do not contain k_sdr, k_hdr, and gamma, so we use reasonable defaults (1.f).
  - The images are in Display P3 but we convert to sRGB linear. Using P3 always looked worse, though...
  - The display part does not adapt to the display at all (-> Rec.2020 PQ), which is the most likely
    source of problems.
  - Use a color management library, e.g. Windows Imaging Component.
  - The test pattern looks fine, but we don't really know what we're expecting.
    Create a test JPG where we know the exact values and therefore the expected
    converted values, too.
- Display proper preview in screen saver settings dialog (see below).
- /preload mode: Fix spurious crashes
- /preload mode: Fix arrow keys only working properly after second image
- Reduce memory footprint for /preload (already much better but still pretty excessive)
- Reduce dependencies: Remove Skia? Remove DX?
- License

## Build Configuration

### Use Skia or GDI+
The image loading codepath can use either GDI+ or Skia.
- `#define USE_SKIA` - Enables loading the images with Skia instead of GDI+ (default: enabled)

### Preview Mode
Preview mode can be disabled.
- `#define DISABLE_PREVIEW_MODE` - Disables preview mode entirely (default: enabled)
- When disabled, the preview window in the Windows settings dialog will be empty
- The #define is enabled by default because preview mode
  - makes the windows screen saver settings dialog take very long to load, and
  - creates zombie processes.

## HDR JPEG Images

The JPEG files to be displayed by the screensaver are created by Lightroom, with HDR enabled, and exported as HDR JPEG in Display P3 color space as described here:
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
- `CMakeLists.txt` - Build configuration
- `README.md` - Project documentation

## Build Instructions

### Prerequisites
- Windows 11
- CMake (https://cmake.org/)
- Visual Studio Build Tools 2022 (or newer) with C++ Desktop Development tools
- Adobe XMP Toolkit SDK (see below for instructions)
- (Optional) Skia (see below for instructions)

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

3. The resulting `.scr` file will be in `build/Release/`. Use the provided install script to register as a screensaver.

#### Notes for Visual Studio Code Users
- You do **not** need the full Visual Studio IDE, only the Build Tools 2022 with C++ support.
- You can use the CMake Tools extension for VS Code for an integrated experience.

## Usage

### Command Line Modes
- `/c` or `/c:parent_hwnd` - Configuration dialog
- `/p` or `/p:parent_hwnd` - Preview mode (for Windows screensaver preview)
- `/s` - Screensaver mode (activated by Windows, exits on mouse movement or any key except special hotkeys)
- `/x` - Standalone mode (for testing, only exits on ESC key)

**Note:** Running the screensaver without arguments will display a help message with all available options.

### Command Line Options
- `/preload` - Enable image caching (overrides registry setting)
  - Example: `HDRScreenSaver.scr /x /preload` (standalone mode with caching enabled)
  - Example: `HDRScreenSaver.scr /s /preload` (screensaver mode with caching enabled)
- `/r` - Enable random order (overrides registry setting)
  - Example: `HDRScreenSaver.scr /x /r` (standalone mode with random order enabled)
  - Example: `HDRScreenSaver.scr /s /r` (screensaver mode with random order enabled)

### Image Display
- The screensaver displays images from the configured folder
- Images are loaded with Skia, converted to Rec.2020 linear, and uploaded as a 16-bit float texture for HDR output
- All color management is handled by Skia. The DirectX pipeline only displays the final values

### Special Hotkeys (work in all modes)
- **ESC** - Exit screensaver
- **Left Arrow** - Previous image
- **Right Arrow** - Next image
- **H/S** - Toggle between HDR and SDR display (if image has gain map)
- **G** - Toggle gain map display (if image has gain map)

## Configuration Dialog

- The screensaver supports a configuration dialog (`/c` mode) to select the folder containing your HDR images.
- The default folder is your Windows "Pictures" folder.
- The selected folder is saved in the registry and used for all modes (screensaver, preview, standalone).
- You can change the folder at any time by running the configuration dialog again.

### Settings Options

- **Image folder**: Select the folder containing your HDR JPEG images
- **Display seconds**: How long each image is displayed before advancing to the next
- **Max cache (MB)**: Maximum memory usage for image caching (minimum 128 MB)
- **Enable image caching**: When enabled, images are preloaded asynchronously for smoother transitions. When disabled (default), images are loaded synchronously when needed, reducing memory usage but may cause brief pauses when advancing to new images.
- **Include subfolders**: When enabled (default), images from all subfolders of the selected folder will be included in the slideshow
- **Randomize order**: When enabled, images are displayed in random order instead of sequentially
  - Right arrow and automatic advancement select random images
  - Left arrow navigates back through history (last 1000 images viewed)
- **Enable logging**: Toggle logging to file
- **Log file path**: Location of the log file

## XMP Toolkit SDK Integration (for HDR Gain Map)

To extract gain map parameters robustly from HDR JPEGs, this project uses the [Adobe XMP Toolkit SDK](https://github.com/adobe/XMP-Toolkit-SDK/).
The SDK has to be built manually, no prebuilt binaries available as of today (2025-07).

### How to obtain and build the XMP Toolkit SDK
1. Clone the SDK:
   ```powershell
   git clone https://github.com/adobe/XMP-Toolkit-SDK.git
   ```
2. Follow the build instructions in the SDK's README for Windows (typically using CMake and Visual Studio).
3. After building, set the `XMP_TOOLKIT_DIR` environment variable to point to your XMP Toolkit SDK installation:
   ```powershell
   set XMP_TOOLKIT_DIR=C:\path\to\XMP-Toolkit-SDK
   ```
   Or modify the paths in `CMakeLists.txt` to match your installation.

---

### Skia Dependency (Optional)
To use the Skia backend, install Skia via vcpkg:

1. Open PowerShell
2. Clone vcpkg:
   ```powershell
   git clone https://github.com/microsoft/vcpkg.git
   ```
3. Bootstrap vcpkg:
   ```powershell
   .\bootstrap-vcpkg.bat
   ```
4. Install Skia:
   ```powershell
   .\vcpkg.exe install skia
   ```

After installing, configure your CMake project to use vcpkg by adding the following to your CMake configure command:
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake
```
Replace `C:path\to\vcpkg` with the actual path to your vcpkg directory.

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contributing

We welcome contributions! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on how to contribute to this project.

---

_Last updated: July 4, 2025_