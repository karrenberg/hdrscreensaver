@echo off
echo Testing HDRScreenSaver with disabled preview mode...
echo.
echo This test verifies that preview mode is disabled by the DISABLE_PREVIEW_MODE flag.
echo.

echo Step 1: Kill any existing processes
taskkill /F /IM HDRScreenSaver.exe >NUL 2>&1
taskkill /F /IM HDRScreenSaver.scr >NUL 2>&1
timeout /t 2 >NUL

echo Step 2: Test preview mode (should be disabled)
echo Testing preview mode with DISABLE_PREVIEW_MODE flag:
echo.
cd /d "%~dp0build\Release"
HDRScreenSaver.scr.exe /p 12345
echo.
echo Preview mode test completed (should have returned immediately).
echo.

echo Step 3: Test standalone mode (should work normally)
echo Testing standalone mode - press ESC to exit:
echo.
HDRScreenSaver.scr.exe /x
echo.
echo Standalone mode exited.
echo.

echo Step 4: Test configuration dialog (should work normally)
echo Testing configuration dialog:
echo.
HDRScreenSaver.scr.exe /c
echo.
echo Configuration dialog closed.
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
echo Now test the Windows screensaver settings dialog:
echo 1. Open Windows Settings > Personalization > Lock screen > Screen saver settings
echo 2. Select HDRScreenSaver from the dropdown
echo 3. The preview window should be empty/black (preview mode is disabled)
echo 4. Click "Settings" - this should open the configuration dialog
echo 5. Close the configuration dialog
echo 6. Close the screen saver settings dialog
echo 7. Check Task Manager to ensure no HDRScreenSaver processes remain
echo.
echo The preview window being empty should prevent the zombie process issue.
echo.
pause 