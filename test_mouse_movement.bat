@echo off
echo Testing HDRScreenSaver mouse movement detection...
echo.
echo This test verifies that the screensaver exits on mouse movement.
echo.
echo Instructions:
echo 1. The screensaver will start in screensaver mode (/s)
echo 2. Move your mouse to exit the screensaver
echo 3. If it doesn't exit, press any key to test key exit
echo.
echo Starting screensaver mode...
echo (Move mouse to exit)
echo.
cd /d "%~dp0build\Release"
HDRScreenSaver.scr.exe /s
echo.
echo Screensaver exited.
echo.
echo Test complete.
pause 