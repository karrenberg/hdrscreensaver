// WebView2Mode.cpp - WebView2-based slideshow implementation

#include <windows.h>
#include <shlwapi.h>
#include <wrl.h>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <random>
#include <chrono>

#include <webview2.h>

#include "Logger.h"
#include "SettingsDialog.h"
#include "WebView2Mode.h"
#include "ImageFileUtils.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shlwapi.lib")

using Microsoft::WRL::ComPtr;

// Globals for the low-level keyboard hook used to forward hotkeys while WebView2 has focus
static HHOOK g_wv2_kbHook = nullptr;
static HWND g_wv2_host_hwnd = nullptr;
// Thread id for the RunWebView2Mode message loop so the hook can forward messages
static DWORD g_wv2_thread_id = 0;

// Custom message to forward keys from the low-level hook into the RunWebView2Mode loop
static const UINT WM_APP_HOTKEY = WM_APP + 1;
// Custom message to forward mouse movement from low-level hook into the RunWebView2Mode loop
static const UINT WM_APP_MOUSEMOVE = WM_APP + 2;

// Globals for the low-level mouse hook
static HHOOK g_wv2_mouseHook = nullptr;
static POINT g_wv2_initial_mouse_pos = {0,0};
// Record last navigation key (VK_LEFT/VK_RIGHT). Default to VK_RIGHT.
static volatile LONG g_wv2_last_nav_key = VK_RIGHT;

static void SetLastNavKey(UINT vk)
{
    InterlockedExchange(&g_wv2_last_nav_key, (LONG)vk);
}

// Low-level keyboard proc: forwards specified keys to the saver host window when the host or one of
// its child windows is the foreground window. Returns 1 to swallow the event when handled.
static LRESULT CALLBACK WV2_LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* k = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        UINT vk = (UINT)k->vkCode;
        HWND fg = GetForegroundWindow();
        if (!fg) return CallNextHookEx(g_wv2_kbHook, nCode, wParam, lParam);

        // Only intercept when the foreground window is our saver host or a child of it
        if (!g_wv2_host_hwnd || !(fg == g_wv2_host_hwnd || IsChild(g_wv2_host_hwnd, fg))) {
            return CallNextHookEx(g_wv2_kbHook, nCode, wParam, lParam);
        }

        bool shouldHandle = false;
        WPARAM postKey = 0;
        switch (vk) {
            case VK_LEFT: postKey = VK_LEFT; shouldHandle = true; break;
            case VK_RIGHT: postKey = VK_RIGHT; shouldHandle = true; break;
            case VK_ESCAPE: postKey = VK_ESCAPE; shouldHandle = true; break;
            case 'H': /* H */ postKey = 'H'; shouldHandle = true; break;
            case 'S': /* S */ postKey = 'S'; shouldHandle = true; break;
            case 'G': /* G */ postKey = 'G'; shouldHandle = true; break;
            default: break;
        }

        if (shouldHandle) {
            // Forward to the RunWebView2Mode thread if known, otherwise post WM_KEY messages to host window
            if (g_wv2_thread_id != 0) {
                if (!PostThreadMessageW(g_wv2_thread_id, WM_APP_HOTKEY, (WPARAM)postKey, 0)) {
                    LOG_MSG(L"WebView2Mode Hook: PostThreadMessageW failed to post hotkey");
                }
            } else {
                HWND target = g_wv2_host_hwnd ? g_wv2_host_hwnd : fg;
                PostMessageW(target, WM_KEYDOWN, (WPARAM)postKey, 0);
                PostMessageW(target, WM_KEYUP, (WPARAM)postKey, 0);
            }
            return 1; // swallow event
        }
    }
    return CallNextHookEx(g_wv2_kbHook, nCode, wParam, lParam);
}

// Low-level mouse proc: detect mouse movement while our host is foreground and forward it
static LRESULT CALLBACK WV2_LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && wParam == WM_MOUSEMOVE) {
        MSLLHOOKSTRUCT* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        if (!ms) return CallNextHookEx(g_wv2_mouseHook, nCode, wParam, lParam);

        // If we don't have a host window recorded, skip
        if (!g_wv2_host_hwnd) return CallNextHookEx(g_wv2_mouseHook, nCode, wParam, lParam);

        // Check whether the mouse is inside our host window rect (screen coords)
        RECT hostRect; GetWindowRect(g_wv2_host_hwnd, &hostRect);
        POINT pt = ms->pt;
        if (PtInRect(&hostRect, pt)) {
            // If the cursor moved from the recorded initial position, forward a message
            if (pt.x != g_wv2_initial_mouse_pos.x || pt.y != g_wv2_initial_mouse_pos.y) {
                if (g_wv2_thread_id != 0) {
                    if (!PostThreadMessageW(g_wv2_thread_id, WM_APP_MOUSEMOVE, 0, 0)) {
                        LOG_MSG(L"WebView2Mode Hook: PostThreadMessageW failed to post mouse-move");
                    }
                } else {
                    PostMessageW(g_wv2_host_hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));
                }
            }
        }
    }
    return CallNextHookEx(g_wv2_mouseHook, nCode, wParam, lParam);
}

struct WV2State {
    HWND hwnd = nullptr;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    bool requestReinit = false;
    bool sdrMode = false;
    HHOOK kbHook = nullptr; // low-level keyboard hook handle
    EventRegistrationToken accelToken{};
    EventRegistrationToken downloadToken{};
};

// Helper: install low-level hooks and initialize globals
static void InstallLowLevelHooks(WV2State& s, const POINT& initialMousePos)
{
    g_wv2_host_hwnd = s.hwnd;
    g_wv2_thread_id = GetCurrentThreadId();
    g_wv2_initial_mouse_pos = initialMousePos;

    if (!g_wv2_kbHook) {
        g_wv2_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, WV2_LowLevelKeyboardProc, GetModuleHandle(nullptr), 0);
        if (g_wv2_kbHook) s.kbHook = g_wv2_kbHook;
        else LOG_MSG(L"WebView2Mode: Failed to install keyboard hook");
    }

    if (!g_wv2_mouseHook) {
        g_wv2_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, WV2_LowLevelMouseProc, GetModuleHandle(nullptr), 0);
        if (!g_wv2_mouseHook) LOG_MSG(L"WebView2Mode: Failed to install mouse hook");
    }
}

// Helper: remove low-level hooks and clear globals
static void UninstallLowLevelHooks(WV2State& s)
{
    if (g_wv2_kbHook) {
        UnhookWindowsHookEx(g_wv2_kbHook);
        g_wv2_kbHook = nullptr;
        s.kbHook = nullptr;
    }
    if (g_wv2_mouseHook) {
        UnhookWindowsHookEx(g_wv2_mouseHook);
        g_wv2_mouseHook = nullptr;
    }
    g_wv2_host_hwnd = nullptr;
    g_wv2_thread_id = 0;
}

static void RemoveAcceleratorIfAny(WV2State& s)
{
    if (s.controller && s.accelToken.value != 0) {
        s.controller->remove_AcceleratorKeyPressed(s.accelToken);
        s.accelToken = {};
    }
}

static void RemoveDownloadHandlerIfAny(WV2State& s)
{
    if (s.webview && s.downloadToken.value != 0) {
        ComPtr<ICoreWebView2_4> webview4;
        if (SUCCEEDED(s.webview.As(&webview4)) && webview4) {
            webview4->remove_DownloadStarting(s.downloadToken);
        }
        s.downloadToken = {};
    }
}

static std::wstring ToFileUri(const std::wstring& path)
{
    wchar_t buf[32768];
    DWORD len = ARRAYSIZE(buf);
    if (SUCCEEDED(UrlCreateFromPathW(path.c_str(), buf, &len, 0))) {
        return std::wstring(buf, len);
    }
    std::wstring p = path;
    for (auto& ch : p) if (ch == L'\\') ch = L'/';
    return L"file:///" + p;
}

static LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WV2State* state = reinterpret_cast<WV2State*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    switch (msg)
    {
    case WM_SIZE:
        if (state && state->controller) {
            RECT rc; GetClientRect(hwnd, &rc);
            state->controller->put_Bounds(rc);
        }
        break;
    case WM_SETFOCUS:
        if (state && state->controller) {
            state->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) {
            if (state && state->controller) {
                state->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
            }
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Helper: ensure host window and WebView have programmatic focus
static void SetHostFocus(WV2State& s)
{
    if (!s.hwnd) return;
    SetForegroundWindow(s.hwnd);
    SetActiveWindow(s.hwnd);
    SetFocus(s.hwnd);
    if (s.controller) s.controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

 static bool CreateHostWindow(WV2State& s, const LPCSTR title, bool fullscreen)
{
     const LPCSTR kClassName = "HDRWebView2HostForSaver";
    WNDCLASSEX wc{ sizeof(WNDCLASSEX) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = HostWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kClassName;
    if (!RegisterClassEx(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        LOG_MSG(L"WebView2Mode: RegisterClassEx failed");
        return false;
    }
     DWORD style = fullscreen ? (WS_POPUP | WS_VISIBLE) : (WS_OVERLAPPEDWINDOW | WS_VISIBLE);
     int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 1280, h = 800;
     if (fullscreen) {
         x = 0; y = 0;
         w = GetSystemMetrics(SM_CXSCREEN);
         h = GetSystemMetrics(SM_CYSCREEN);
     }
     s.hwnd = CreateWindowEx(fullscreen ? WS_EX_TOPMOST : 0, kClassName, title, style,
                             x, y, w, h,
                             nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    if (!s.hwnd) {
        LOG_MSG(L"WebView2Mode: CreateWindowEx failed");
        return false;
    }
    SetWindowLongPtr(s.hwnd, GWLP_USERDATA, (LONG_PTR)&s);
     if (fullscreen) {
         SetWindowPos(s.hwnd, HWND_TOPMOST, 0, 0, w, h, SWP_SHOWWINDOW);
         ShowCursor(FALSE);
     }
    return true;
}

static bool InitWebView2(WV2State& s)
{
    // Provide browser args via environment var to support older SDKs.
    // Set flags to disable HDR display if requested.
    // Set conservative flags to disable telemetry, background/networking,
    // automatic updates and crash reporting.
    const wchar_t* kTelemetryFlags =
        L"--disable-background-networking --disable-breakpad --disable-component-update "
        L"--disable-client-side-phishing-detection --disable-domain-reliability --disable-crash-reporter "
        L"--safebrowsing-disable-auto-update --disable-features=AutofillServerCommunication,NetworkPrediction";

    if (s.sdrMode) {
        std::wstring args = std::wstring(kTelemetryFlags) + L" --force-color-profile=srgb --disable-hdr";
        SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", args.c_str());
    } else {
        SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", kTelemetryFlags);
    }

    // Use a distinct user data folder per mode so a fresh environment is created with new args
    wchar_t localAppData[MAX_PATH] = {0};
    DWORD envLen = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (envLen == 0 || envLen >= MAX_PATH) {
        wcscpy_s(localAppData, L".");
    }
    std::wstring userDataDir = std::wstring(localAppData) + L"\\HDRScreenSaverWV2\\" + (s.sdrMode ? L"SDR" : L"HDR");
    std::filesystem::create_directories(userDataDir);

    LOG_MSG(L"WebView2Mode: InitWebView2 with userDataDir=" + userDataDir + L", mode=" + std::wstring(s.sdrMode ? L"SDR" : L"HDR"));

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userDataDir.c_str(), nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [&s](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    LOG_MSG(L"WebView2Mode: Environment creation failed");
                    PostQuitMessage(1);
                    return S_OK;
                }
                env->CreateCoreWebView2Controller(
                    s.hwnd,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [&s](HRESULT result2, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result2) || !controller) {
                                LOG_MSG(L"WebView2Mode: Controller creation failed");
                                PostQuitMessage(1);
                                return S_OK;
                            }
                            s.controller = controller;
                            if (FAILED(s.controller->get_CoreWebView2(&s.webview)) || !s.webview) {
                                LOG_MSG(L"WebView2Mode: Failed to get CoreWebView2");
                                PostQuitMessage(1);
                                return S_OK;
                            }
                            LOG_MSG(L"WebView2Mode: Controller and WebView created");
                            RECT rc; GetClientRect(s.hwnd, &rc);
                            s.controller->put_Bounds(rc);
                            ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(s.webview->get_Settings(&settings)) && settings) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(FALSE);
                                settings->put_IsZoomControlEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                            }

                            // Register a DownloadStarting handler to prevent unwanted "download" behavior
                            // for some image types (for example .tif/.jxl) and instead skip to the next
                            // file in current navigation direction.
                            ComPtr<ICoreWebView2_4> webview4;
                            if (SUCCEEDED(s.webview.As(&webview4)) && webview4) {
                                webview4->add_DownloadStarting(
                                    Microsoft::WRL::Callback<ICoreWebView2DownloadStartingEventHandler>(
                                        [&s](ICoreWebView2* /*unused*/, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
                                            if (!args) return S_OK;
                                            // Get the download operation and source URI
                                            ComPtr<ICoreWebView2DownloadOperation> op;
                                            if (SUCCEEDED(args->get_DownloadOperation(&op)) && op) {
                                                LPWSTR uriRaw = nullptr;
                                                if (SUCCEEDED(op->get_Uri(&uriRaw)) && uriRaw) {
                                                    std::wstring uri(uriRaw);
                                                    CoTaskMemFree(uriRaw);

                                                    // Cancel the default download behavior and mark handled.
                                                    args->put_Cancel(TRUE);
                                                    args->put_Handled(TRUE);
                                                    // Decide direction based on last navigation
                                                    UINT advanceKey = (g_wv2_last_nav_key == VK_LEFT) ? VK_LEFT : VK_RIGHT;
                                                    // Log the skip action (URI and direction)
                                                    LOG_MSG(L"WebView2Mode: Skipping unsupported image: " , uri , L" -> direction=" , (advanceKey == VK_LEFT ? L"LEFT" : L"RIGHT"));
                                                    // Immediately advance by posting a hotkey message with the chosen direction
                                                    if (g_wv2_thread_id != 0) {
                                                        if (!PostThreadMessageW(g_wv2_thread_id, WM_APP_HOTKEY, (WPARAM)advanceKey, 0)) {
                                                            LOG_MSG(L"WebView2Mode: Skipping unsupported image: failed to advance to next image");
                                                        }
                                                    } else {
                                                        if (s.hwnd) {
                                                            PostMessageW(s.hwnd, WM_KEYDOWN, (WPARAM)advanceKey, 0);
                                                            PostMessageW(s.hwnd, WM_KEYUP, (WPARAM)advanceKey, 0);
                                                        }
                                                    }
                                                }
                                            }
                                            // If we obtained a deferral but didn't complete it above, ensure release
                                            // (def->Complete called where appropriate). The ComPtr will release on exit.
                                            return S_OK;
                                        }).Get(),
                                    &s.downloadToken);
                            }

                            EventRegistrationToken navStartToken{};
                            s.webview->add_NavigationStarting(
                                Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                        LPWSTR uri = nullptr;
                                        if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
                                            std::wstring u(uri);
                                            LOG_MSG(std::wstring(L"WebView2Mode: NavigationStarting -> ") + u);
                                            // Allow only file:// URIs
                                            if (u.rfind(L"file://", 0) != 0) {
                                                args->put_Cancel(TRUE);
                                                LOG_MSG(L"WebView2Mode: Navigation canceled for non-file URI: ", u.c_str());
                                            }
                                            CoTaskMemFree(uri);
                                        }
                                        return S_OK;
                                    }).Get(),
                                &navStartToken);

                            EventRegistrationToken navCompletedToken{};
                            s.webview->add_NavigationCompleted(
                                Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                        BOOL isSuccess = FALSE; args->get_IsSuccess(&isSuccess);
                                        COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                                        args->get_WebErrorStatus(&status);
                                        LOG_MSG(std::wstring(L"WebView2Mode: NavigationCompleted -> ") + (isSuccess ? L"success" : L"failure") + L", status=" + std::to_wstring((int)status));
                                        return S_OK;
                                    }).Get(),
                                &navCompletedToken);
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    return SUCCEEDED(hr);
}

int RunWebView2Mode(bool shutdownOnAnyUnhandledInput, const ScreenSaverSettings& settings, const std::wstring& singleImagePath /*= L""*/, bool disableAutoAdvance /*= false*/)
{
    std::vector<std::wstring> imageFiles;
    std::wstring startingImage = L"";

    if (!singleImagePath.empty()) {
        if (!std::filesystem::exists(singleImagePath) || !std::filesystem::is_regular_file(singleImagePath)) {
            MessageBoxW(nullptr, (L"HDRScreenSaver: Image file not found:\n" + singleImagePath).c_str(), L"HDRScreenSaver", MB_OK);
            return 1;
        }
        startingImage = singleImagePath;
        // Try to load all images from the same folder as the provided image so arrow keys can navigate
        try {
            std::filesystem::path p(singleImagePath);
            std::filesystem::path parent = p.parent_path();
            if (!parent.empty() && std::filesystem::exists(parent)) {
                imageFiles = GetImageFilesInFolder(parent.wstring(), settings.includeSubfolders);
            }
        } catch (...) {
            // Fall back to single image only
        }
        if (imageFiles.empty()) {
            // Fallback: show only the provided file
            imageFiles.push_back(singleImagePath);
        }
    } else {
        if (!std::filesystem::exists(settings.imageFolder)) {
            MessageBoxW(nullptr, (L"HDRScreenSaver: Image folder not found:\n" + settings.imageFolder).c_str(), L"HDRScreenSaver", MB_OK);
            return 1;
        }
        imageFiles = GetImageFilesInFolder(settings.imageFolder, settings.includeSubfolders);
        if (imageFiles.empty()) {
            LOG_MSG(L"No images found in folder: " + settings.imageFolder);
            return 1;
        }
    }

    // Determine start index if a starting image was provided
    size_t startIndex = 0;
    if (!startingImage.empty()) {
        for (size_t i = 0; i < imageFiles.size(); ++i) {
            bool matched = false;
            try {
                if (std::filesystem::equivalent(imageFiles[i], startingImage))
                    matched = true;
            } catch (...) {
                // fall back to string compare
                if (imageFiles[i] == startingImage)
                    matched = true;
            }
            if (matched) {
                startIndex = i;
                break;
            }
        }
    }

    WV2State s;
    const bool fullscreen = shutdownOnAnyUnhandledInput; // fullscreen for screensaver mode
    if (!CreateHostWindow(s, "HDRScreenSaver - WebView2", fullscreen)) return 1;

    // (no interactive debug popup here) ensure thread id recorded for hook forwarding
    g_wv2_thread_id = GetCurrentThreadId();

    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needUninit = SUCCEEDED(hrCo);

    if (!InitWebView2(s)) {
        if (needUninit) CoUninitialize();
        return 1;
    }

    // Wait for asynchronous WebView2 initialization to complete
    {
        const auto waitStart = std::chrono::steady_clock::now();
        const auto maxWait = std::chrono::seconds(10);
        // Record our thread id so the keyboard hook can forward messages here
        g_wv2_thread_id = GetCurrentThreadId();
        while (!s.webview || !s.controller) {
            MSG m;
            while (PeekMessage(&m, nullptr, 0, 0, PM_REMOVE)) {
                if (m.message == WM_QUIT) {
                    if (needUninit) CoUninitialize();
                    return 1;
                }
                TranslateMessage(&m);
                DispatchMessage(&m);
            }
            if (std::chrono::steady_clock::now() - waitStart > maxWait) {
                LOG_MSG(L"WebView2Mode: Timeout waiting for WebView2 to initialize (controller/webview is null)");
                if (needUninit) CoUninitialize();
                return 1;
            }
            Sleep(10);
        }
        LOG_MSG(L"WebView2Mode: WebView2 initialized successfully");
    }

    // Navigation helpers
    auto navigateTo = [&](size_t index) {
        if (!s.webview) {
            LOG_MSG(L"WebView2Mode: navigateTo called before webview ready");
            return;
        }
        const std::wstring uri = ToFileUri(imageFiles[index]);
        s.webview->Navigate(uri.c_str());
        LOG_MSG(L"WebView2Mode: Showing " + imageFiles[index]);
    };

    // State for navigation
    size_t currentIndex = startIndex;
    std::vector<size_t> history; history.reserve(1024);
    size_t historyPosition = 0;
    const size_t maxHistorySize = 1000;
    std::random_device rd; std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, imageFiles.size() - 1);

    auto getNextRandomIndex = [&]() -> size_t {
        size_t nextIndex = dist(gen);
        if (historyPosition < history.size()) history.resize(historyPosition);
        history.push_back(currentIndex);
        if (history.size() > maxHistorySize) history.erase(history.begin());
        historyPosition = history.size();
        return nextIndex;
    };

    // Ensure focus is on our host and WebView for immediate keyboard handling
    SetHostFocus(s);

    // First navigation once ready
    navigateTo(currentIndex);

    // Consolidated key handler used by accelerator callback and forwarded hotkey messages
    auto handleKey = [&](UINT key, WV2State& state) -> bool {
        bool handled = false;
        if (key == VK_ESCAPE) {
            PostQuitMessage(0);
            handled = true;
        } else if (key == VK_RIGHT) {
            // record navigation direction so DownloadStarting knows user intent
            SetLastNavKey(VK_RIGHT);
            if (settings.randomizeOrder) currentIndex = getNextRandomIndex();
            else currentIndex = (currentIndex + 1) % imageFiles.size();
            navigateTo(currentIndex);
            handled = true;
        } else if (key == VK_LEFT) {
            // record navigation direction so DownloadStarting knows user intent
            SetLastNavKey(VK_LEFT);
            if (settings.randomizeOrder) {
                if (historyPosition > 0) { historyPosition--; currentIndex = history[historyPosition]; }
            } else {
                currentIndex = (currentIndex + imageFiles.size() - 1) % imageFiles.size();
            }
            navigateTo(currentIndex);
            handled = true;
        } else if (key == VK_DOWN || key == 'H' || key == 'h' || key == 'S' || key == 's') {
            state.sdrMode = !state.sdrMode;
            state.requestReinit = true;
            LOG_MSG(std::wstring(L"WebView2Mode: Hotkey H/S toggled. New mode: ") + (state.sdrMode ? L"SDR" : L"HDR"));
            handled = true;
        } else if (shutdownOnAnyUnhandledInput) {
            PostQuitMessage(0);
            handled = true;
        }
        return handled;
    };

    // Consolidated accelerator handler installer
    auto attachAccelerator = [&](WV2State& state) {
        if (!state.controller) return;
        if (state.accelToken.value != 0) {
            // Ensure any previous handler is removed before adding a new one
            state.controller->remove_AcceleratorKeyPressed(state.accelToken);
            state.accelToken = {};
        }
        state.controller->add_AcceleratorKeyPressed(
            Microsoft::WRL::Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
            [&](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                Microsoft::WRL::ComPtr<ICoreWebView2AcceleratorKeyPressedEventArgs2> args2;
                if (SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&args2))) && args2) {
                    args2->put_IsBrowserAcceleratorKeyEnabled(FALSE);
                }
                COREWEBVIEW2_KEY_EVENT_KIND kind{}; args->get_KeyEventKind(&kind);
                UINT key = 0; args->get_VirtualKey(&key);
                if (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN || kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
                    bool handled = handleKey(key, state);
                    if (handled) args->put_Handled(TRUE);
                }
                return S_OK;
            }).Get(),
        &state.accelToken);
    };

    // Attach for the first time
    attachAccelerator(s);

    // Mouse movement exit like existing saver
    POINT initialMousePos{0,0}; GetCursorPos(&initialMousePos);
    bool mouseMoved = false;

    const auto advanceInterval = std::chrono::seconds(settings.displaySeconds);
    // If disableAutoAdvance is requested (open-with single file), we'll skip the automatic advancement.
    const bool autoAdvanceEnabled = !disableAutoAdvance;
    auto lastAdvance = std::chrono::steady_clock::now();

    MSG msg;
    bool running = true;
    // Install low-level keyboard and mouse hooks
    InstallLowLevelHooks(s, initialMousePos);
    while (running)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }

            // Forwarded hotkey from low-level hook
            if (msg.message == WM_APP_HOTKEY) {
                WPARAM key = msg.wParam;
                if (handleKey((UINT)key, s)) continue;
            }

            if (msg.message == WM_APP_MOUSEMOVE && shutdownOnAnyUnhandledInput) {
                // Mouse moved according to low-level hook; perform same shutdown logic as WM_MOUSEMOVE
                PostQuitMessage(0);
                mouseMoved = true;
                continue;
            }
            if (msg.message == WM_MOUSEMOVE && shutdownOnAnyUnhandledInput) {
                POINT pt; GetCursorPos(&pt);
                if (!mouseMoved && (pt.x != initialMousePos.x || pt.y != initialMousePos.y)) {
                    PostQuitMessage(0);
                    mouseMoved = true;
                }
            }

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!running) break;

        // Handle SDR/HDR reinit request
        if (s.requestReinit) {
            s.requestReinit = false;
            LOG_MSG(std::wstring(L"WebView2Mode: Reinitializing WebView2 for mode: ") + (s.sdrMode ? L"SDR" : L"HDR"));
            // Save current bounds
            RECT curBounds{0,0,0,0};
            if (s.controller) s.controller->get_Bounds(&curBounds);
            LOG_MSG(L"WebView2Mode: Current bounds before reinit: left=" + std::to_wstring(curBounds.left) + L", top=" + std::to_wstring(curBounds.top) + L", right=" + std::to_wstring(curBounds.right) + L", bottom=" + std::to_wstring(curBounds.bottom));
            // Tear down existing controller/webview and unregister handlers
            RemoveAcceleratorIfAny(s);
            RemoveDownloadHandlerIfAny(s);
            s.webview.Reset();
            s.controller.Reset();
            // Uninstall low-level hooks prior to reinit
            UninstallLowLevelHooks(s);
            // Recreate with requested mode
            if (!InitWebView2(s)) { running = false; break; }
            // Wait until ready
            {
                const auto t0 = std::chrono::steady_clock::now();
                while (!s.webview || !s.controller) {
                    MSG m; while (PeekMessage(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessage(&m); }
                    if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(5)) break;
                    Sleep(5);
                }
            }

            // Ensure focus is on our host and WebView for immediate keyboard handling
            SetHostFocus(s);
            if (s.controller) s.controller->put_Bounds(curBounds);
            LOG_MSG(L"WebView2Mode: Reinit complete. Restored bounds and reattached handlers. Navigating to current image.");

            // Reattach accelerator handler
            attachAccelerator(s);

            // Navigate to current image again
            navigateTo(currentIndex);

            // Reinstall low-level hooks after reinit
            POINT curPos{0,0}; GetCursorPos(&curPos);
            InstallLowLevelHooks(s, curPos);
        }

        if (autoAdvanceEnabled && std::chrono::steady_clock::now() - lastAdvance >= advanceInterval) {
            lastAdvance = std::chrono::steady_clock::now();
            // record automatic forward navigation
            SetLastNavKey(VK_RIGHT);
            if (settings.randomizeOrder) currentIndex = getNextRandomIndex();
            else currentIndex = (currentIndex + 1) % imageFiles.size();
            navigateTo(currentIndex);
        }

        Sleep(10);
    }

    if (needUninit) CoUninitialize();
    // Ensure hooks are removed on exit
    UninstallLowLevelHooks(s);
    // Remove accelerator registration if present
    RemoveAcceleratorIfAny(s);
    // Remove download handler if present
    RemoveDownloadHandlerIfAny(s);
    return 0;
}
