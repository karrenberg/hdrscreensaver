@echo off
:: create_installer.bat - stage build outputs and invoke Inno Setup (ISCC.exe)
:: Edit ISCC_PATH below if Inno Setup is installed elsewhere.

:: Default path where Inno Setup installs ISCC.exe
set "ISCC_PATH=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"

:: Resolve script directory (installer folder)
set "SCRIPT_DIR=%~dp0"

:: Paths
set "REPO_ROOT=%SCRIPT_DIR%..\\"
set "SRC_BUILD=%REPO_ROOT%build\Release"
set "DST_BUILD=%SCRIPT_DIR%build\Release"
set "ISS_FILE=%SCRIPT_DIR%HDRScreenSaver.iss"

echo === HDRScreenSaver: Create installer helper ===
echo Script dir: %SCRIPT_DIR%
echo Repo root: %REPO_ROOT%
echo Source build: %SRC_BUILD%
echo Staging build: %DST_BUILD%

if exist "%SRC_BUILD%" goto :SRC_OK
echo ERROR: Source build directory not found: %SRC_BUILD%
echo Build the project in Release before creating the installer.
exit /b 1
:SRC_OK

if exist "%ISS_FILE%" goto :ISS_OK
echo ERROR: Inno Setup script not found: %ISS_FILE%
exit /b 1
:ISS_OK

REM Clean/create staging folder
if exist "%DST_BUILD%" rd /s /q "%DST_BUILD%"
mkdir "%DST_BUILD%" 2>nul
if errorlevel 1 (
    echo ERROR: Failed to create staging folder: %DST_BUILD%
    exit /b 1
)

echo Copying build outputs to staging...

:: Copy main exe (built as HDRScreenSaver.scr.exe) -> keep same name in staging
if exist "%SRC_BUILD%\HDRScreenSaver.scr.exe" (
    copy /Y "%SRC_BUILD%\HDRScreenSaver.scr.exe" "%DST_BUILD%" >nul
) else echo WARNING: HDRScreenSaver.scr.exe not found in %SRC_BUILD%

:: Copy launcher
if exist "%SRC_BUILD%\HDRScreenSaverLauncher.scr.exe" (
    copy /Y "%SRC_BUILD%\HDRScreenSaverLauncher.scr.exe" "%DST_BUILD%" >nul
) else echo WARNING: HDRScreenSaverLauncher.scr.exe not found in %SRC_BUILD%

:: Copy WebView2 bootstrapper from third-party if present (common path used in this repo)
set "WEBBOOT_SRC=%REPO_ROOT%third-party\microsoft.web.webview2.1.0.3485.44\MicrosoftEdgeWebView2RuntimeInstallerX64.exe"
set "WEBBOOT_DST=%DST_BUILD%\MicrosoftEdgeWebView2RuntimeInstallerX64.exe"
if exist "%WEBBOOT_SRC%" (
    copy /Y "%WEBBOOT_SRC%" "%WEBBOOT_DST%" >nul
) else (
    powershell -NoProfile -Command "try { Invoke-WebRequest -Uri 'https://go.microsoft.com/fwlink/p/?LinkId=2124703' -OutFile '%WEBBOOT_DST%' -UseBasicParsing; exit 0 } catch { exit 1 }"
    if %errorlevel% neq 0 (
        echo ERROR: Failed to obtain WebView2 bootstrapper.
        if exist "%WEBBOOT_DST%" del /f /q "%WEBBOOT_DST%"
        exit /b 2
    )
)

echo Staging complete. Files in %DST_BUILD%:
dir /b "%DST_BUILD%"

REM Verify ISCC exists
if not exist "%ISCC_PATH%" (
    echo WARNING: ISCC.exe not found at "%ISCC_PATH%". Falling back to PATH lookup.
    set "ISCC_PATH=ISCC.exe"
)

echo Invoking Inno Setup compiler...
"%ISCC_PATH%" "%ISS_FILE%"
if errorlevel 1 (
    echo ERROR: Inno Setup compiler failed: %errorlevel%
    exit /b %errorlevel%
)

echo Installer build succeeded.
exit /b 0
