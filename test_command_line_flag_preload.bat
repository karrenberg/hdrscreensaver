@echo off
echo Testing HDRScreenSaver command line flag -preload...
echo.
echo This script demonstrates the -preload flag to enable image caching.
echo.
echo Available modes:
echo   /c - Configuration dialog
echo   /p - Preview mode  
echo   /s - Screensaver mode
echo   /x - Standalone mode
echo.
echo The -preload flag can be used with any mode to enable image caching.
echo.
echo Examples:
echo   HDRScreenSaver.scr /x -preload     (standalone with caching enabled)
echo   HDRScreenSaver.scr /s -preload     (screensaver with caching enabled)
echo.
echo Press any key to run standalone mode with caching enabled...
pause >nul
echo.
echo Running: HDRScreenSaver.scr /x -preload
echo (Press ESC to exit)
echo.
cd /d "%~dp0build\Release"
HDRScreenSaver.scr.exe /x -preload
echo.
echo Screensaver exited.
pause 