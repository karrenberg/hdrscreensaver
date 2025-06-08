@echo off
echo Testing HDRScreenSaver -preload flag...
echo.
echo This test verifies that the -preload flag works correctly
echo and doesn't conflict with the /c configuration dialog flag.
echo.
echo Testing configuration dialog (should open settings):
echo HDRScreenSaver.scr /c
echo.
pause
cd /d "%~dp0build\Release"
HDRScreenSaver.scr.exe /c
echo.
echo Configuration dialog closed.
echo.
echo Testing standalone mode with preload enabled:
echo HDRScreenSaver.scr /x -preload
echo (Press ESC to exit)
echo.
HDRScreenSaver.scr.exe /x -preload
echo.
echo Test complete.
pause 