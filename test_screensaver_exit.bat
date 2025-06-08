@echo off
echo Testing HDRScreenSaver exit behavior...
echo.
echo This test demonstrates the different exit behaviors:
echo.
echo Screensaver mode (/s):
echo - Exits on mouse movement
echo - Exits on any key except special hotkeys
echo - Special hotkeys: ESC, Left/Right arrows, H/S, G
echo.
echo Standalone mode (/x):
echo - Only exits on ESC key
echo - All other keys and mouse movement are ignored
echo.
echo Press any key to test screensaver mode...
echo (Move mouse or press any key to exit)
pause >nul
echo.
echo Running screensaver mode (/s):
echo.
cd /d "%~dp0build\Release"
HDRScreenSaver.scr.exe /s
echo.
echo Screensaver mode exited.
echo.
echo Press any key to test standalone mode...
echo (Only ESC will exit)
pause >nul
echo.
echo Running standalone mode (/x):
echo.
HDRScreenSaver.scr.exe /x
echo.
echo Standalone mode exited.
echo.
echo Test complete.
pause 