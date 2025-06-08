// HDRScreenSaver Launcher
// Minimal Windows launcher for HDRScreenSaver.scr
// Forwards all arguments to the real screensaver binary in Program Files
#include <windows.h>
#include <string>
#include <iostream>

// Entry point: launches the real screensaver and waits for it to exit.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Path to the real screensaver binary (update as needed)
    const wchar_t* realExe = L"C:\\Program Files\\HDRScreenSaver\\HDRScreenSaver.exe";

    // Get the command line arguments (excluding the program name)
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring args;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) args += L" ";
        args += L'"';
        args += argv[i];
        args += L'"';
    }
    LocalFree(argv);

    // Build the command line: "realExe" [args]
    std::wstring cmdLine = L"\"";
    cmdLine += realExe;
    cmdLine += L"\" ";
    cmdLine += args;

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    BOOL success = CreateProcessW(
        realExe, // lpApplicationName
        &cmdLine[0], // lpCommandLine
        NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi
    );
    if (!success) {
        MessageBoxW(NULL, L"Failed to launch HDRScreenSaver.exe", L"Error", MB_ICONERROR);
        return 1;
    }
    // Wait for the real screensaver to exit
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exitCode;
}
