cmake_minimum_required(VERSION 3.24)
project(RsShellExplorer VERSION 0.1.0 LANGUAGES CXX RC)

add_library(RsShell.Explorer SHARED
    dllmain.cpp
    OpenInAppCommand.cpp
    SlidePreviewHandler.cpp
    SlideThumbnailProvider.cpp
    Resource.rc
    RsShell.Explorer.def)

set_target_properties(RsShell.Explorer PROPERTIES
    OUTPUT_NAME "RsShell.Explorer"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)

target_compile_definitions(RsShell.Explorer PRIVATE
    _WIN32_WINNT=0x0A00
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE)

target_compile_options(RsShell.Explorer PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)

target_link_libraries(RsShell.Explorer PRIVATE ole32 shlwapi user32 gdi32)

# RsSlideCABI（Swift 动态库）的 import 库——缩略图解码为硬依赖，需先构建 RsSlide：
#   cd ../RsSlide && swift build -c release --product RsSlideCABI
set(RSSLIDE_CABI_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../RsSlide/.build/release/RsSlideCABI.lib")
if (NOT EXISTS "${RSSLIDE_CABI_LIB}")
    message(FATAL_ERROR "RsSlideCABI.lib not found: ${RSSLIDE_CABI_LIB}\nBuild RsSlide first: cd ../RsSlide; swift build -c release --product RsSlideCABI")
endif ()
target_link_libraries(RsShell.Explorer PRIVATE "${RSSLIDE_CABI_LIB}")

# 缩略图链路进程内测试台（开发工具，不随包分发；build-msix.ps1 只按名收录产物）
add_executable(RsShell.ThumbTest ThumbTest.cpp)

set_target_properties(RsShell.ThumbTest PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)

target_compile_definitions(RsShell.ThumbTest PRIVATE
    _WIN32_WINNT=0x0A00
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE)

target_compile_options(RsShell.ThumbTest PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)

target_link_libraries(RsShell.ThumbTest PRIVATE ole32 shlwapi user32 gdi32 windowscodecs)
