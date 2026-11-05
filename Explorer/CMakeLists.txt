cmake_minimum_required(VERSION 3.24)
project(RsShellExplorer VERSION 0.1.0 LANGUAGES CXX RC)

add_library(RsShell.Explorer SHARED
    dllmain.cpp
    OpenInAppCommand.cpp
    Resource.rc
    RsShell.Explorer.def)

set_target_properties(RsShell.Explorer PROPERTIES
    OUTPUT_NAME "RsShell.Explorer"
    # 静态 CRT：包内 surrogate 加载时不可依赖框架包里的动态 CRT
    # （清单的 VCLibs.140.00 只有 *_app.dll，普通名在 .UWPDesktop 变体）
    MSVC_RUNTIME_LIBRARY "MultiThreaded")

target_compile_definitions(RsShell.Explorer PRIVATE
    _WIN32_WINNT=0x0A00
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE)

target_compile_options(RsShell.Explorer PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)

target_link_libraries(RsShell.Explorer PRIVATE ole32 shlwapi user32)
