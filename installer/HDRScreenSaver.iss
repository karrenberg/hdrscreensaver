; HDRScreenSaver installer (Inno Setup)
; Use installer\create_installer.bat to stage files and build the installer.

[Setup]
AppName=HDRScreenSaver
AppVersion=1.0.0
DefaultDirName={commonpf}\HDRScreenSaver
DisableProgramGroupPage=yes
Compression=lzma
SolidCompression=yes
PrivilegesRequired=admin
OutputBaseFilename=HDRScreenSaver_Installer
ArchitecturesInstallIn64BitMode=x64os
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; Main exe built as HDRScreenSaver.scr.exe in the build output -> install as HDRScreenSaver.exe
Source: "build\Release\HDRScreenSaver.scr.exe"; DestDir: "{app}"; DestName: "HDRScreenSaver.exe"; Flags: ignoreversion

; Launcher to be the screensaver file in System32 (required for Windows to list it as a screensaver)
Source: "build\Release\HDRScreenSaverLauncher.scr.exe"; DestDir: "{win}\System32"; DestName: "HDRScreenSaver.scr"; Flags: ignoreversion

; Include WebView2 bootstrapper
Source: "build\Release\MicrosoftEdgeWebView2RuntimeInstallerX64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\HDRScreenSaver"; Filename: "{app}\HDRScreenSaver.exe"; WorkingDir: "{app}"; IconFilename: "{app}\HDRScreenSaver.exe"

[Run]
; Run WebView2 bootstrapper silently
Filename: "{tmp}\MicrosoftEdgeWebView2RuntimeInstallerX64.exe"; Parameters: "/silent /install"; StatusMsg: "Installing WebView2 runtime..."; Flags: runhidden

[UninstallDelete]
Type: filesandordirs; Name: "{win}\System32\HDRScreenSaver.scr"
Type: filesandordirs; Name: "{app}"

[UninstallRun]
; Remove per-user settings for the user who runs the uninstaller
Filename: "reg.exe"; Parameters: "delete HKCU\Software\HDRScreenSaver /f"; Flags: runhidden; RunOnceId: "RemoveHKCU"