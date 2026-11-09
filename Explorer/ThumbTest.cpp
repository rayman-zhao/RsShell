// 缩略图链路进程内测试台：不经 Explorer / 系统缩略图缓存 / surrogate，按 Shell
// 的真实调用顺序直接驱动 RsShell.Explorer.dll（DllGetClassObject → IPersistFile →
// IExtractImage），逐步打印 HRESULT 与耗时，结果位图落盘 PNG。
// 与 DLL 同目录需有 RsSlideCABI.dll（含 Swift 运行时），通常把本 exe 拷进消费方
// 包根（build-msix.ps1 的 package 目录）或用 --dll 指过去。
// 用法：RsShell.ThumbTest.exe <切片文件> [--dll <RsShell.Explorer.dll>]
//       [--size N] [--out <png>]
#include <windows.h>

#include <fcntl.h>
#include <io.h>
#include <objbase.h>
#include <shlwapi.h>
#include <wincodec.h>

#include <cstdarg>
#include <cstdio>
#include <string>

#include "SlideThumbnailProvider.h"

namespace {

struct Options {
    std::wstring slide;
    std::wstring dllPath;
    std::wstring outPng;
    UINT size = 256;
};

void PrintUsageAndExit(int code) {
    wprintf(L"usage: RsShell.ThumbTest.exe <slide file> [--dll <RsShell.Explorer.dll>]\n");
    wprintf(L"       [--size N] [--out <png>]\n");
    exit(code);
}

Options ParseOptions(int argc, wchar_t** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--dll" && i + 1 < argc) {
            o.dllPath = argv[++i];
        } else if (arg == L"--size" && i + 1 < argc) {
            o.size = static_cast<UINT>(_wtoi(argv[++i]));
        } else if (arg == L"--out" && i + 1 < argc) {
            o.outPng = argv[++i];
        } else if (arg == L"--help" || arg == L"-h") {
            PrintUsageAndExit(0);
        } else if (!arg.empty() && arg[0] == L'-') {
            wprintf(L"unknown option: %s\n", arg.c_str());
            PrintUsageAndExit(2);
        } else {
            o.slide = arg;
        }
    }
    if (o.slide.empty() || o.size == 0) PrintUsageAndExit(2);
    if (o.outPng.empty()) o.outPng = o.slide + L".thumb.png";
    return o;
}

int g_stepIndex = 0;

void Step(const wchar_t* format, ...) {
    wchar_t line[1024] = {};
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(line, _TRUNCATE, format, args);
    va_end(args);
    wprintf(L"[%2d] %s\n", ++g_stepIndex, line);
}

bool FileExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring DirOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

HRESULT SaveBitmapToPng(HBITMAP bitmap, const std::wstring& path) {
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return hr;

    IWICBitmap* source = nullptr;
    hr = factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapIgnoreAlpha, &source);
    if (SUCCEEDED(hr)) {
        IStream* stream = nullptr;
        hr = SHCreateStreamOnFileW(path.c_str(), STGM_CREATE | STGM_WRITE, &stream);
        if (SUCCEEDED(hr)) {
            IWICBitmapEncoder* encoder = nullptr;
            hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
            if (SUCCEEDED(hr)) {
                hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
                if (SUCCEEDED(hr)) {
                    IWICBitmapFrameEncode* frame = nullptr;
                    hr = encoder->CreateNewFrame(&frame, nullptr);
                    if (SUCCEEDED(hr)) {
                        hr = frame->Initialize(nullptr);
                        if (SUCCEEDED(hr)) hr = frame->WriteSource(source, nullptr);
                        if (SUCCEEDED(hr)) hr = frame->Commit();
                        frame->Release();
                    }
                    if (SUCCEEDED(hr)) hr = encoder->Commit();
                }
                encoder->Release();
            }
            stream->Release();
        }
        source->Release();
    }
    factory->Release();
    return hr;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    // 宽输出走 UTF-8：Git Bash / Windows Terminal 直读，conhost 配 65001 也正确
    _setmode(_fileno(stdout), _O_U8TEXT);
    SetConsoleOutputCP(CP_UTF8);

    const Options opt = ParseOptions(argc, argv);
    const ULONGLONG startTick = GetTickCount64();

    Step(L"slide: %s (size=%u)", opt.slide.c_str(), opt.size);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Step(L"CoInitializeEx(STA) hr=0x%08lX", static_cast<unsigned long>(hr));
    if (FAILED(hr)) return 1;

    // DLL 定位：默认与测试 exe 同目录（包根拷贝场景），否则 --dll 显式指定
    std::wstring dllPath = opt.dllPath;
    if (dllPath.empty()) {
        wchar_t self[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, self, ARRAYSIZE(self));
        dllPath = DirOf(self) + L"\\RsShell.Explorer.dll";
    }
    Step(L"dll: %s", dllPath.c_str());
    if (!FileExists(dllPath)) {
        Step(L"FAIL: RsShell.Explorer.dll not found");
        return 1;
    }
    if (!FileExists(DirOf(dllPath) + L"\\RsSlideCABI.dll")) {
        Step(L"WARN: RsSlideCABI.dll not next to the dll - decode will fail");
    }

    HMODULE dll = LoadLibraryExW(dllPath.c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    Step(L"LoadLibrary %s", dll ? L"OK" : L"FAIL");
    if (!dll) {
        Step(L"FAIL: GLE=%lu", GetLastError());
        return 1;
    }

    using DllGetClassObjectFn = HRESULT (STDMETHODCALLTYPE*)(REFCLSID, REFIID, void**);
    const auto getClassObject = reinterpret_cast<DllGetClassObjectFn>(
        GetProcAddress(dll, "DllGetClassObject"));
    if (!getClassObject) {
        Step(L"FAIL: DllGetClassObject not exported");
        return 1;
    }

    IClassFactory* factory = nullptr;
    hr = getClassObject(CLSID_SlideThumbnailProvider, IID_PPV_ARGS(&factory));
    Step(L"DllGetClassObject(CLSID_SlideThumbnailProvider) hr=0x%08lX",
         static_cast<unsigned long>(hr));
    if (FAILED(hr)) return 1;

    IPersistFile* persist = nullptr;
    hr = factory->CreateInstance(nullptr, IID_PPV_ARGS(&persist));
    Step(L"CreateInstance -> IPersistFile hr=0x%08lX", static_cast<unsigned long>(hr));
    factory->Release();
    if (FAILED(hr)) return 1;

    IExtractImage2* extract = nullptr;
    hr = persist->QueryInterface(IID_PPV_ARGS(&extract));
    Step(L"QI IExtractImage2 hr=0x%08lX", static_cast<unsigned long>(hr));
    if (FAILED(hr)) {
        persist->Release();
        return 1;
    }

    hr = persist->Load(opt.slide.c_str(), STGM_READ);
    Step(L"IPersistFile::Load hr=0x%08lX", static_cast<unsigned long>(hr));
    persist->Release();
    if (FAILED(hr)) {
        extract->Release();
        return 1;
    }

    wchar_t location[MAX_PATH] = {};
    DWORD priority = 0;
    DWORD flags = 0;
    SIZE requested = {static_cast<LONG>(opt.size), static_cast<LONG>(opt.size)};
    hr = extract->GetLocation(location, ARRAYSIZE(location), &priority, &requested, 32, &flags);
    Step(L"GetLocation hr=0x%08lX path=[%s] flags=0x%lX", static_cast<unsigned long>(hr),
         location, flags);
    if (FAILED(hr)) {
        extract->Release();
        return 1;
    }

    HBITMAP bitmap = nullptr;
    const ULONGLONG tick = GetTickCount64();
    hr = extract->Extract(&bitmap);
    Step(L"Extract hr=0x%08lX in %lums", static_cast<unsigned long>(hr),
         static_cast<unsigned long>(GetTickCount64() - tick));

    FILETIME stamp = {};
    const HRESULT dateHr = extract->GetDateStamp(&stamp);
    extract->Release();
    Step(L"GetDateStamp hr=0x%08lX", static_cast<unsigned long>(dateHr));

    if (FAILED(hr) || !bitmap) {
        Step(L"FAIL: no bitmap (hr=0x%08lX)", static_cast<unsigned long>(hr));
        return 1;
    }

    BITMAP info = {};
    GetObjectW(bitmap, sizeof(info), &info);
    Step(L"bitmap %ldx%ld", info.bmWidth, info.bmHeight);

    hr = SaveBitmapToPng(bitmap, opt.outPng);
    DeleteObject(bitmap);
    Step(L"save %s hr=0x%08lX", opt.outPng.c_str(), static_cast<unsigned long>(hr));
    if (FAILED(hr)) return 1;

    const auto canUnload = reinterpret_cast<HRESULT (STDMETHODCALLTYPE*)()>(
        GetProcAddress(dll, "DllCanUnloadNow"));
    if (canUnload) {
        Step(L"DllCanUnloadNow hr=0x%08lX", static_cast<unsigned long>(canUnload()));
    }

    Step(L"PASS in %.1fs", static_cast<double>(GetTickCount64() - startTick) / 1000.0);
    CoUninitialize();
    return 0;
}
