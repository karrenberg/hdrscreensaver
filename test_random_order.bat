@echo off
echo Testing HDRScreenSaver random order feature...
echo.
echo This test verifies that the random order feature works correctly.
echo.

echo Step 1: Kill any existing processes
taskkill /F /IM HDRScreenSaver.exe >NUL 2>&1
taskkill /F /IM HDRScreenSaver.scr >NUL 2>&1
timeout /t 2 >NUL

echo Step 2: Test configuration dialog
echo Opening configuration dialog to test the new "Randomize order" checkbox:
echo.
cd /d "%~dp0build\Release"
HDRScreenSaver.scr.exe /c
echo.
echo Configuration dialog closed.
echo.

echo Step 3: Test standalone mode with random order enabled via command line
echo Testing standalone mode with /r flag (random order enabled):
echo (Press Right Arrow to advance randomly, Left Arrow to go back in history, ESC to exit)
echo.
HDRScreenSaver.scr.exe /x /r
echo.
echo Standalone mode with random order exited.
echo.

echo Step 4: Test standalone mode with random order disabled
echo Testing standalone mode without random order (sequential):
echo (Press Right Arrow to advance sequentially, Left Arrow to go back, ESC to exit)
echo.
HDRScreenSaver.scr.exe /x
echo.
echo Standalone mode with sequential order exited.
echo.

echo Step 5: Check for any remaining processes
echo Checking for any remaining HDRScreenSaver processes...
tasklist /FI "IMAGENAME eq HDRScreenSaver.exe" 2>NUL | find /I "HDRScreenSaver.exe" >NUL
if %errorlevel%==0 (
    echo WARNING: Found remaining HDRScreenSaver.exe processes!
    tasklist /FI "IMAGENAME eq HDRScreenSaver.exe"
) else (
    echo No HDRScreenSaver.exe processes found - good!
)

echo.
echo Test complete.
echo.
echo Random order feature summary:
echo - When enabled: Right arrow and timeout advance to random images
echo - When enabled: Left arrow goes back through history (last 1000 images)
echo - When disabled: Normal sequential navigation
echo - Command line flag /r enables random order
echo - Setting is saved in registry and persists between sessions
echo.
pause 