cmake_minimum_required(VERSION 3.15)
project(HDRScreenSaver)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenMP REQUIRED)

# Set static CRT for all targets (Release/Debug)
#set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Set dynamic CRT for all targets (Release/Debug)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

include_directories(include)
file(GLOB SOURCES "src/*.cpp")
list(REMOVE_ITEM SOURCES "${CMAKE_SOURCE_DIR}/src/LauncherScr.cpp")

## Use vcpkg toolchain if available
if(DEFINED CMAKE_TOOLCHAIN_FILE)
    message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
endif()
## Find Skia via vcpkg (optional, for Skia backend)
find_package(unofficial-skia CONFIG REQUIRED)

# Add executable and link libraries
add_executable(HDRScreenSaver WIN32 ${SOURCES})

set_target_properties(HDRScreenSaver PROPERTIES OUTPUT_NAME "HDRScreenSaver.scr")

# Always use the KEYWORD signature for target_link_libraries to avoid CMake errors
# Link GDI+ (default Windows backend)
# Link comctl32 for settings dialog stuff (e.g. __imp_InitCommonControlsEx)
target_link_libraries(HDRScreenSaver PRIVATE gdiplus comctl32)

# Link OpenMP
if(OpenMP_CXX_FOUND)
    target_link_libraries(HDRScreenSaver PRIVATE OpenMP::OpenMP_CXX)
endif()

# Link Skia if found
if(TARGET unofficial::skia::skia)
    target_link_libraries(HDRScreenSaver PRIVATE unofficial::skia::skia)
    target_compile_definitions(HDRScreenSaver PRIVATE USE_SKIA_BACKEND)
endif()

# Adobe XMP Toolkit SDK integration
# Set these environment variables or modify the paths below to point to your XMP Toolkit SDK installation
if(DEFINED ENV{XMP_TOOLKIT_DIR})
    set(XMP_TOOLKIT_SHARED_DIR "$ENV{XMP_TOOLKIT_DIR}/public/libraries/windows_x64/Release")
    set(XMP_TOOLKIT_SHARED_DEBUG_DIR "$ENV{XMP_TOOLKIT_DIR}/public/libraries/windows_x64/Debug")
    set(XMP_TOOLKIT_INCLUDE_DIR "$ENV{XMP_TOOLKIT_DIR}")
else()
    # Default paths - modify these to match your XMP Toolkit SDK installation
    set(XMP_TOOLKIT_SHARED_DIR "C:/path/to/XMP-Toolkit-SDK/public/libraries/windows_x64/Release")
    set(XMP_TOOLKIT_SHARED_DEBUG_DIR "C:/path/to/XMP-Toolkit-SDK/public/libraries/windows_x64/Debug")
    set(XMP_TOOLKIT_INCLUDE_DIR "C:/path/to/XMP-Toolkit-SDK")
    message(WARNING "XMP_TOOLKIT_DIR environment variable not set. Please set it to your XMP Toolkit SDK installation path or modify the paths above.")
endif()
include_directories(${XMP_TOOLKIT_INCLUDE_DIR})

# Link XMPFiles and XMPCore (required for XMP parsing)
# Link DirectX libraries for DX11 backend
target_link_libraries(HDRScreenSaver PRIVATE
    # Dynamic XMP Toolkit SDK
    $<$<CONFIG:Debug>:${XMP_TOOLKIT_SHARED_DEBUG_DIR}/XMPFiles.lib>
    $<$<CONFIG:Release>:${XMP_TOOLKIT_SHARED_DIR}/XMPFiles.lib>
    $<$<CONFIG:Debug>:${XMP_TOOLKIT_SHARED_DEBUG_DIR}/XMPCore.lib>
    $<$<CONFIG:Release>:${XMP_TOOLKIT_SHARED_DIR}/XMPCore.lib>
    # Static XMP Toolkit SDK
    #$<$<CONFIG:Debug>:${XMP_TOOLKIT_SHARED_DEBUG_DIR}/XMPFilesStatic.lib>
    #$<$<CONFIG:Release>:${XMP_TOOLKIT_SHARED_DIR}/XMPFilesStatic.lib>
    #$<$<CONFIG:Debug>:${XMP_TOOLKIT_SHARED_DEBUG_DIR}/XMPCoreStatic.lib>
    #$<$<CONFIG:Release>:${XMP_TOOLKIT_SHARED_DIR}/XMPCoreStatic.lib>
    d3d11 dxgi d3dcompiler
)


# Enable XMP Toolkit usage in code
add_definitions(-DWIN_ENV)

# Post-build: Copy XMP Toolkit DLLs to output directories for Debug and Release. Otherwise we get a CRT library runtime error.
# (Not necessary for skia, somehow that dll gets copied automatically)
add_custom_command(TARGET HDRScreenSaver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${XMP_TOOLKIT_SHARED_DEBUG_DIR}/XMPCore.dll"
        $<TARGET_FILE_DIR:HDRScreenSaver>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${XMP_TOOLKIT_SHARED_DEBUG_DIR}/XMPFiles.dll"
        $<TARGET_FILE_DIR:HDRScreenSaver>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${XMP_TOOLKIT_SHARED_DIR}/XMPCore.dll"
        $<TARGET_FILE_DIR:HDRScreenSaver>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${XMP_TOOLKIT_SHARED_DIR}/XMPFiles.dll"
        $<TARGET_FILE_DIR:HDRScreenSaver>
)

# Add launcher executable
add_executable(LauncherScr WIN32 src/LauncherScr.cpp)
set_target_properties(LauncherScr PROPERTIES OUTPUT_NAME "HDRScreenSaverLauncher.scr")
# Statically link CRT for launcher (so it doesn't need DLLs)
set_target_properties(LauncherScr PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")

target_sources(HDRScreenSaver PRIVATE src/SettingsDialogTemplate.rc)
