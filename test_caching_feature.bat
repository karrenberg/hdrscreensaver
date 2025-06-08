@echo off
echo Testing HDRScreenSaver caching disable feature...
echo.
echo This will open the configuration dialog where you can:
echo 1. Set the image folder
echo 2. Enable/disable image caching
echo 3. Adjust other settings
echo.
echo The "Enable image caching" checkbox is the new feature.
echo When enabled, images will be preloaded asynchronously for smoother transitions.
echo When disabled (default), images will be loaded synchronously when needed.
echo.
pause
echo.
echo Opening configuration dialog...
cd /d "%~dp0build\Release"
HDRScreenSaver.scr.exe /c
echo.
echo Configuration dialog closed.
pause 