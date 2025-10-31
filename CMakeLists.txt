cmake_minimum_required(VERSION 3.15)
project(HDRScreenSaver)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set static CRT for all targets (Release/Debug)
#set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Set dynamic CRT for all targets (Release/Debug)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

include_directories(include)
file(GLOB SOURCES "src/*.cpp")
list(REMOVE_ITEM SOURCES "${CMAKE_SOURCE_DIR}/src/LauncherScr.cpp")

# Add executable and link libraries
add_executable(HDRScreenSaver WIN32 ${SOURCES} resources/HDRScreensaver.rc)

set_target_properties(HDRScreenSaver PROPERTIES OUTPUT_NAME "HDRScreenSaver.scr")

# ---------------------------------------------------------------------
# Link comctl32 for settings dialog stuff (e.g. __imp_InitCommonControlsEx)
# ---------------------------------------------------------------------
target_link_libraries(HDRScreenSaver PRIVATE comctl32)

# ---------------------------------------------------------------------
# Find and link WebView2 loader library
# ---------------------------------------------------------------------
set(WEBVIEW2_ROOT "${CMAKE_SOURCE_DIR}/third-party/microsoft.web.webview2.1.0.3485.44/build/native")

include_directories("${WEBVIEW2_ROOT}/include")

# Try to locate WebView2 SDK installed in default location used by installer
# Default header path: C:/Program Files (x86)/Microsoft WebView2 SDK/<version>/include
# Default lib path:    C:/Program Files (x86)/Microsoft WebView2 SDK/<version>/lib/<arch>
# We will search for a matching folder automatically.

if(EXISTS "${WEBVIEW2_ROOT}")
  message(STATUS "Found WebView2 SDK root: ${WEBVIEW2_ROOT}")
  set(WV2_INCLUDE_DIR "${WEBVIEW2_ROOT}/include")
  # choose lib folder based on generator arch
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(WV2_LIB_DIR "${WEBVIEW2_ROOT}/x64")
  else()
    set(WV2_LIB_DIR "${WEBVIEW2_ROOT}/x86")
  endif()
else()
  message(FATAL_ERROR "WebView2 SDK not found. Set WEBVIEW2_ROOT to the SDK root folder.")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE "${WV2_INCLUDE_DIR}")

# Link the WebView2 loader library (the installer provides WebView2Loader.dll and WebView2LoaderStatic.lib or WebView2Loader.dll with import lib)
# Typical import lib name: WebView2LoaderStatic.lib or WebView2Loader.lib (check lib directory)
file(GLOB WV2_LIB_FILES "${WV2_LIB_DIR}/*.lib")
if(WV2_LIB_FILES)
  # Prefer static loader if available
  list(FILTER WV2_LIB_FILES INCLUDE REGEX "WebView2LoaderStatic.*\\.lib")
  if(NOT WV2_LIB_FILES)
    file(GLOB WV2_LIB_FILES "${WV2_LIB_DIR}/WebView2Loader*.lib")
  endif()
  list(GET WV2_LIB_FILES 0 WV2_LIB_FILE)
  message(STATUS "Linking WebView2 lib: ${WV2_LIB_FILE}")
  target_link_libraries(${PROJECT_NAME} PRIVATE "${WV2_LIB_FILE}")
else()
  message(WARNING "No .lib found in ${WV2_LIB_DIR}. If using the DLL directly, ensure WebView2Loader.dll is available at runtime and link to Win32 APIs (ole32, user32) as needed.")
endif()

# Add launcher executable
add_executable(LauncherScr WIN32 src/LauncherScr.cpp resources/HDRScreensaver.rc)
set_target_properties(LauncherScr PROPERTIES OUTPUT_NAME "HDRScreenSaverLauncher.scr")
# Statically link CRT for launcher (so it doesn't need DLLs)
set_target_properties(LauncherScr PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")

target_sources(HDRScreenSaver PRIVATE "resources/HDRScreensaver.rc" src/SettingsDialogTemplate.rc)
target_sources(LauncherScr PRIVATE "resources/HDRScreensaver.rc")