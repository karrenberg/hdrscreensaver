@echo off
echo Testing HDRScreenSaver subfolder feature...
echo.
echo This test verifies that the "Include subfolders" option works correctly.
echo.

echo Step 1: Kill any existing processes
taskkill /F /IM HDRScreenSaver.exe >NUL 2>&1
taskkill /F /IM HDRScreenSaver.scr >NUL 2>&1
timeout /t 2 >NUL

echo Step 2: Test configuration dialog
echo Opening configuration dialog to test the new "Include subfolders" checkbox:
echo.
cd /d "%~dp0build\Release"
HDRScreenSaver.scr.exe /c
echo.
echo Configuration dialog closed.
echo.

echo Step 3: Test standalone mode with subfolder setting
echo Testing standalone mode with current settings (including subfolder setting):
echo (Press ESC to exit)
echo.
HDRScreenSaver.scr.exe /x
echo.
echo Standalone mode exited.
echo.

echo Step 4: Check for any remaining processes
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
echo The "Include subfolders" feature should now be available in the settings dialog.
echo When enabled (default), the screensaver will load images from all subfolders
echo of the selected folder. When disabled, only images in the selected folder
echo will be loaded.
echo.
pause 