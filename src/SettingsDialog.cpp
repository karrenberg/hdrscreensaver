#include "SettingsDialog.h"
#include <shlobj.h>
#include <commctrl.h>
#include <windows.h>
#include <string>
#include <shlwapi.h> // For PathFileExistsW
#include <Knownfolders.h>
#include <Shlobj.h>

#define IDD_SETTINGS     2000
#define IDC_FOLDER_EDIT 2001
#define IDC_BROWSE_BTN  2002
#define IDC_DISPLAYSEC_EDIT 2003
#define IDC_LOG_ENABLE 2004
#define IDC_LOGPATH_EDIT 2005
#define IDC_LOGPATH_BROWSE 2006
#define IDC_INCLUDE_SUBFOLDERS 2007
#define IDC_RANDOMIZE_ORDER 2008
#define IDC_MAP_OVERLAY 2009
#define IDC_MAP_ZOOM_EDIT   2010
#define IDC_DATE_OVERLAY 2012
#define IDC_MAP_ONLINE   2013

// Map zoom slider range in thousandths-of-a-degree half-extent (slider pos = degrees * 1000).
static const double kMapZoomMin = 0.002;  // degrees half-extent (~440 m across)
static const double kMapZoomMax = 25.0;   // degrees half-extent (~5550 km across)

#pragma comment(lib, "shlwapi.lib")

static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    ScreenSaverSettings* s = reinterpret_cast<ScreenSaverSettings*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
    switch (msg) {
    case WM_INITDIALOG: {
        s = reinterpret_cast<ScreenSaverSettings*>(lParam);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)s);
        SetDlgItemTextW(hDlg, IDC_FOLDER_EDIT, s->imageFolder.c_str());
        SetDlgItemInt(hDlg, IDC_DISPLAYSEC_EDIT, s->displaySeconds, FALSE);
        CheckDlgButton(hDlg, IDC_INCLUDE_SUBFOLDERS, s->includeSubfolders ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_RANDOMIZE_ORDER, s->randomizeOrder ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_MAP_OVERLAY, s->mapOverlayEnabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_DATE_OVERLAY, s->dateOverlayEnabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_MAP_ONLINE, s->mapOnlineMode ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_LOG_ENABLE, s->logEnabled ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemTextW(hDlg, IDC_LOGPATH_EDIT, s->logPath.c_str());
        {
            wchar_t buf[32];
            swprintf(buf, 32, L"%.4g", s->mapZoomDegrees);
            SetDlgItemTextW(hDlg, IDC_MAP_ZOOM_EDIT, buf);
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BROWSE_BTN: {
            BROWSEINFOW bi = {0};
            wchar_t displayName[MAX_PATH] = {0};
            bi.hwndOwner = hDlg;
            bi.lpszTitle = L"Select the folder containing your HDR images:";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            bi.pszDisplayName = displayName;
            PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t path[MAX_PATH];
                if (SHGetPathFromIDListW(pidl, path)) {
                    SetDlgItemTextW(hDlg, IDC_FOLDER_EDIT, path);
                }
                CoTaskMemFree(pidl);
            }
            break;
        }
        case IDC_LOGPATH_BROWSE: {
            wchar_t buf[MAX_PATH];
            GetDlgItemTextW(hDlg, IDC_LOGPATH_EDIT, buf, MAX_PATH);
            OPENFILENAMEW ofn = {0};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hDlg;
            ofn.lpstrFile = buf;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrTitle = L"Select log file location";
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            ofn.lpstrDefExt = L"txt";
            if (GetSaveFileNameW(&ofn)) {
                SetDlgItemTextW(hDlg, IDC_LOGPATH_EDIT, buf);
            }
            break;
        }
        case IDOK: {
            wchar_t buf[MAX_PATH];
            GetDlgItemTextW(hDlg, IDC_FOLDER_EDIT, buf, MAX_PATH);
            std::wstring folder = buf;
            if (folder.empty() || !PathFileExistsW(folder.c_str())) {
                MessageBoxW(hDlg, L"Please select a valid image folder.", L"Settings", MB_ICONERROR);
                break;
            }
            BOOL ok = FALSE;
            int sec = (int)GetDlgItemInt(hDlg, IDC_DISPLAYSEC_EDIT, &ok, FALSE);
            if (!ok || sec <= 0) {
                MessageBoxW(hDlg, L"Please enter a valid number of display seconds.", L"Settings", MB_ICONERROR);
                break;
            }
            s->imageFolder = folder;
            s->displaySeconds = sec;
            s->includeSubfolders = (IsDlgButtonChecked(hDlg, IDC_INCLUDE_SUBFOLDERS) == BST_CHECKED);
            s->randomizeOrder = (IsDlgButtonChecked(hDlg, IDC_RANDOMIZE_ORDER) == BST_CHECKED);
            s->mapOverlayEnabled = (IsDlgButtonChecked(hDlg, IDC_MAP_OVERLAY) == BST_CHECKED);
            wchar_t zoomBuf[32] = {0};
            GetDlgItemTextW(hDlg, IDC_MAP_ZOOM_EDIT, zoomBuf, 32);
            double zoom = wcstod(zoomBuf, nullptr);
            if (zoom < kMapZoomMin) zoom = kMapZoomMin;
            if (zoom > kMapZoomMax) zoom = kMapZoomMax;
            s->mapZoomDegrees = zoom;
            s->dateOverlayEnabled = (IsDlgButtonChecked(hDlg, IDC_DATE_OVERLAY) == BST_CHECKED);
            s->mapOnlineMode = (IsDlgButtonChecked(hDlg, IDC_MAP_ONLINE) == BST_CHECKED);
            s->logEnabled = (IsDlgButtonChecked(hDlg, IDC_LOG_ENABLE) == BST_CHECKED);
            GetDlgItemTextW(hDlg, IDC_LOGPATH_EDIT, buf, MAX_PATH);
            s->logPath = buf;
            EndDialog(hDlg, 1);
            break;
        }
        case IDCANCEL:
            EndDialog(hDlg, 0);
            break;
        }
        break;
    }
    return FALSE;
}

bool ShowSettingsDialog(HWND parent, ScreenSaverSettings& settings) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    INT_PTR ret = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS), parent, DlgProc, (LPARAM)&settings);
    return ret == 1;
}

static std::wstring GetDefaultLogPath() {
    PWSTR docPath = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docPath))) {
        result = docPath;
        CoTaskMemFree(docPath);
        result += L"\\HDRScreenSaver.log";
    } else {
        result = L"HDRScreenSaver.log";
    }
    return result;
}

ScreenSaverSettings LoadSettingsFromRegistry() {
    ScreenSaverSettings s;
    HKEY hKey;
    wchar_t buf[MAX_PATH] = {0};
    DWORD len = sizeof(buf);
    s.imageFolder = L"";
    s.displaySeconds = 15;
    s.logEnabled = true;
    s.logPath = L"";
    s.includeSubfolders = true;
    s.randomizeOrder = false;
    s.mapOverlayEnabled = true;
    s.mapZoomDegrees = 3.0;  // ~666 km across (2 * 3 deg * 111 km)
    s.dateOverlayEnabled = true;
    s.mapOnlineMode = true;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\HDRScreenSaver", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"ImageFolder", nullptr, nullptr, (LPBYTE)buf, &len) == ERROR_SUCCESS && wcslen(buf) > 0)
            s.imageFolder = buf;
        DWORD val = 0, sz = sizeof(val);
        if (RegQueryValueExW(hKey, L"DisplaySeconds", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS && val > 0)
            s.displaySeconds = (int)val;
        sz = sizeof(val);
        if (RegQueryValueExW(hKey, L"LogEnabled", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
            s.logEnabled = (val != 0);
        sz = sizeof(val);
        if (RegQueryValueExW(hKey, L"IncludeSubfolders", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
            s.includeSubfolders = (val != 0);
        sz = sizeof(val);
        if (RegQueryValueExW(hKey, L"RandomizeOrder", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
            s.randomizeOrder = (val != 0);
        sz = sizeof(val);
        if (RegQueryValueExW(hKey, L"MapOverlayEnabled", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
            s.mapOverlayEnabled = (val != 0);
        sz = sizeof(val);
        if (RegQueryValueExW(hKey, L"MapZoomX1000", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS && val >= 1 && val <= 25000)
            s.mapZoomDegrees = val / 1000.0;
        else {
            sz = sizeof(val);
            if (RegQueryValueExW(hKey, L"MapZoomX10", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS && val >= 1 && val <= 250)
                s.mapZoomDegrees = val / 10.0;
            else {
                sz = sizeof(val);
                if (RegQueryValueExW(hKey, L"MapZoomDegrees", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS && val > 0 && val <= 25)
                    s.mapZoomDegrees = (double)val;
            }
        }
        sz = sizeof(val);
        if (RegQueryValueExW(hKey, L"DateOverlayEnabled", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
            s.dateOverlayEnabled = (val != 0);
        sz = sizeof(val);
        if (RegQueryValueExW(hKey, L"MapOnlineMode", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
            s.mapOnlineMode = (val != 0);
        len = sizeof(buf);
        if (RegQueryValueExW(hKey, L"LogPath", nullptr, nullptr, (LPBYTE)buf, &len) == ERROR_SUCCESS && wcslen(buf) > 0)
            s.logPath = buf;
        RegCloseKey(hKey);
    }
    if (s.imageFolder.empty()) {
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &path))) {
            s.imageFolder = path;
            CoTaskMemFree(path);
        }
    }
    if (s.logPath.empty()) {
        s.logPath = GetDefaultLogPath();
    }
    return s;
}

void SaveSettingsToRegistry(const ScreenSaverSettings& s) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\HDRScreenSaver", 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"ImageFolder", 0, REG_SZ, (const BYTE*)s.imageFolder.c_str(), (DWORD)((s.imageFolder.size()+1)*sizeof(wchar_t)));
        DWORD val = (DWORD)s.displaySeconds;
        RegSetValueExW(hKey, L"DisplaySeconds", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = (DWORD)(s.logEnabled ? 1 : 0);
        RegSetValueExW(hKey, L"LogEnabled", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = (DWORD)(s.includeSubfolders ? 1 : 0);
        RegSetValueExW(hKey, L"IncludeSubfolders", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = (DWORD)(s.randomizeOrder ? 1 : 0);
        RegSetValueExW(hKey, L"RandomizeOrder", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = (DWORD)(s.mapOverlayEnabled ? 1 : 0);
        RegSetValueExW(hKey, L"MapOverlayEnabled", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = (DWORD)(s.mapZoomDegrees * 1000.0 + 0.5);
        RegSetValueExW(hKey, L"MapZoomX1000", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = (DWORD)(s.dateOverlayEnabled ? 1 : 0);
        RegSetValueExW(hKey, L"DateOverlayEnabled", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        val = (DWORD)(s.mapOnlineMode ? 1 : 0);
        RegSetValueExW(hKey, L"MapOnlineMode", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        RegSetValueExW(hKey, L"LogPath", 0, REG_SZ, (const BYTE*)s.logPath.c_str(), (DWORD)((s.logPath.size()+1)*sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
}
