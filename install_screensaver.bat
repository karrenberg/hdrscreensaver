@echo off
REM HDRScreenSaver Install/Uninstall Script
REM Usage: install_screensaver.bat install|uninstall

:: Check for administrator privileges
>nul 2>&1 net session
if not %errorlevel%==0 (
    echo ERROR: Administrator privileges are required. Please run this script as Administrator.
    exit /b 1
)

setlocal EnableDelayedExpansion
set "APPDIR=C:\Program Files\HDRScreenSaver"
set "SCRNAME=HDRScreenSaver.scr"
set "LAUNCHER=HDRScreenSaverLauncher.scr"
set "SYSTEM32=%WINDIR%\System32"
set "BUILD_DIR=%~dp0build\Release"

if "%1"=="install" goto :install
if "%1"=="uninstall" goto :uninstall

echo Usage: %0 install^|uninstall
exit /b 1

:install
set "COPIED_DLLS=0"
if not exist "%APPDIR%" (
    mkdir "%APPDIR%"
    if errorlevel 1 (
        echo ERROR: Failed to create directory: %APPDIR%
        exit /b 1
    ) else (
        echo Created directory: %APPDIR%
    )
) else (
    echo Directory already exists: %APPDIR%
)

if exist "%BUILD_DIR%\HDRScreenSaver.scr.exe" (
    copy /Y "%BUILD_DIR%\HDRScreenSaver.scr.exe" "%APPDIR%\HDRScreenSaver.exe" >nul
    if errorlevel 1 (
        echo ERROR: Failed to copy HDRScreenSaver.exe to %APPDIR%\
        exit /b 1
    )
) else (
    echo ERROR: %BUILD_DIR%\HDRScreenSaver.scr.exe not found. Build the project first.
    exit /b 1
)

for %%F in ("%BUILD_DIR%\*.dll") do (
    if exist "%%F" (
        copy /Y "%%F" "%APPDIR%\" >nul
        if not errorlevel 1 set /a COPIED_DLLS+=1
    )
)
echo Copied !COPIED_DLLS! DLL(s) to %APPDIR%\

if exist "%BUILD_DIR%\%LAUNCHER%.exe" (
    copy /Y "%BUILD_DIR%\%LAUNCHER%.exe" "%SYSTEM32%\%SCRNAME%" >nul
    if errorlevel 1 (
        echo ERROR: Failed to copy launcher to %SYSTEM32%\%SCRNAME%
        exit /b 1
    )
) else (
    echo ERROR: %BUILD_DIR%\%LAUNCHER%.exe not found. Build the launcher first.
    exit /b 1
)
echo Copied launcher to %SYSTEM32%\%SCRNAME%

echo Installed HDRScreenSaver.
exit /b 0

:uninstall
set "DELETED_DLLS=0"
if exist "%SYSTEM32%\%SCRNAME%" (
    del "%SYSTEM32%\%SCRNAME%"
    if errorlevel 1 (
        echo ERROR: Failed to delete %SYSTEM32%\%SCRNAME%
    ) else (
        echo Deleted %SYSTEM32%\%SCRNAME%
    )
) else (
    echo %SYSTEM32%\%SCRNAME% not found
)

if exist "%APPDIR%\HDRScreenSaver.exe" (
    del "%APPDIR%\HDRScreenSaver.exe"
    if errorlevel 1 (
        echo ERROR: Failed to delete %APPDIR%\HDRScreenSaver.exe
    )
)

for %%F in ("%APPDIR%\*.dll") do (
    if exist "%%F" (
        del "%%F"
        if not errorlevel 1 set /a DELETED_DLLS+=1
    )
)
echo Deleted !DELETED_DLLS! DLL(s) from %APPDIR%\

rd "%APPDIR%" 2>nul
if errorlevel 1 (
    echo Note: %APPDIR% not removed (may not be empty or in use)
) else (
    echo Removed directory: %APPDIR%
)

REM Remove registry key for image folder config
reg delete "HKCU\Software\HDRScreenSaver" /f >nul 2>&1

echo Uninstalled HDRScreenSaver.
exit /b 0
