@echo off
echo HDRScreenSaver Command Line Options
echo ===================================
echo.
echo Modes:
echo   /c     - Configuration dialog
echo   /p     - Preview mode (for Windows screensaver preview)
echo   /s     - Screensaver mode (activated by Windows)
echo   /x     - Standalone mode (for testing)
echo.
echo Options:
echo   -preload     - Enable image caching (overrides registry setting)
echo.
echo Examples:
echo   HDRScreenSaver.scr /c          - Open settings dialog
echo   HDRScreenSaver.scr /x          - Run in standalone mode
echo   HDRScreenSaver.scr /x -preload - Run standalone with caching enabled
echo   HDRScreenSaver.scr /s -preload - Run screensaver with caching enabled
echo.
echo Testing invalid mode (should show error):
echo.
cd /d "%~dp0build\Release"
HDRScreenSaver.scr.exe /invalid
echo.
echo Test complete.
pause 