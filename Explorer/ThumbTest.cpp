// 缩略图/预览窗格/属性处理器链路进程内测试台：不经 Explorer / 系统缩略图缓存 /
// surrogate，按 Shell 的真实调用顺序直接驱动 RsShell.Explorer.dll（缩略图：
// DllGetClassObject → IPersistFile → IExtractImage；预览：DllGetClassObject →
// IInitializeWithFile → IPreviewHandler；属性：DllGetClassObject →
// IInitializeWithFile → IPropertyStore），逐步打印 HRESULT 与耗时，结果位图落盘 PNG。
// 与 DLL 同目录需有 RsSlideCABI.dll（含 Swift 运行时），通常把本 exe 拷进消费方
// 包根（build-msix.ps1 的 package 目录）或用 --dll 指过去。
// 用法：RsShell.ThumbTest.exe <切片文件> [--preview] [--props] [--dll <RsShell.Explorer.dll>]
//       [--size N] [--out <png>]
#include <windows.h>

#include <fcntl.h>
#include <io.h>
#include <objbase.h>
#include <propkey.h>
#include <propsys.h>
#include <shlwapi.h>
#include <wincodec.h>

#include <cstdarg>
#include <cstdio>
#include <string>

#include "SlidePreviewHandler.h"
#include "SlidePropertyStore.h"
#include "SlideThumbnailProvider.h"

namespace {

struct Options {
    std::wstring slide;
    std::wstring dllPath;
    std::wstring outPng;
    UINT size = 256;
    bool preview = false;
    bool props = false;
};

void PrintUsageAndExit(int code) {
    wprintf(
        L"usage: RsShell.ThumbTest.exe <slide file> [--preview] [--props] [--dll <RsShell.Explorer.dll>]\n");
    wprintf(L"       [--size N] [--out <png>]\n");
    exit(code);
}

Options ParseOptions(int argc, wchar_t** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--dll" && i + 1 < argc) {
            o.dllPath = argv[++i];
        } else if (arg == L"--preview") {
            o.preview = true;
        } else if (arg == L"--props") {
            o.props = true;
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
    if (o.outPng.empty()) {
        o.outPng = o.slide + (o.preview ? L".preview.png" : L".thumb.png");
    }
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

void PumpMessages(DWORD milliseconds) {
    const ULONGLONG until = GetTickCount64() + milliseconds;
    MSG msg = {};
    while (GetTickCount64() < until) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    }
}

constexpr wchar_t kHostWindowClass[] = L"RsShell.ThumbTestHost";

// 预览窗格替身：普通顶层窗口充当 IPreviewHandler::SetWindow 的宿主父窗口
HWND CreateHostWindow(int width, int height) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kHostWindowClass;
    RegisterClassExW(&wc);
    return CreateWindowExW(0, kHostWindowClass, L"ThumbTest preview host",
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height,
                           nullptr, nullptr, wc.hInstance, nullptr);
}

using DllGetClassObjectFn = HRESULT (STDMETHODCALLTYPE*)(REFCLSID, REFIID, void**);

int RunThumbnailFlow(DllGetClassObjectFn getClassObject, const Options& opt) {
    IClassFactory* factory = nullptr;
    HRESULT hr = getClassObject(CLSID_SlideThumbnailProvider, IID_PPV_ARGS(&factory));
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
    return FAILED(hr) ? 1 : 0;
}

int RunPreviewFlow(DllGetClassObjectFn getClassObject, const Options& opt) {
    IClassFactory* factory = nullptr;
    HRESULT hr = getClassObject(CLSID_SlidePreviewHandler, IID_PPV_ARGS(&factory));
    Step(L"DllGetClassObject(CLSID_SlidePreviewHandler) hr=0x%08lX",
         static_cast<unsigned long>(hr));
    if (FAILED(hr)) return 1;

    IInitializeWithFile* fileInit = nullptr;
    hr = factory->CreateInstance(nullptr, IID_PPV_ARGS(&fileInit));
    Step(L"CreateInstance -> IInitializeWithFile hr=0x%08lX", static_cast<unsigned long>(hr));
    factory->Release();
    if (FAILED(hr)) return 1;

    IPreviewHandler* preview = nullptr;
    hr = fileInit->QueryInterface(IID_PPV_ARGS(&preview));
    Step(L"QI IPreviewHandler hr=0x%08lX", static_cast<unsigned long>(hr));
    if (FAILED(hr)) {
        fileInit->Release();
        return 1;
    }

    // QI 契约反向验证：Stream 必须不可达，否则 Shell 会改走流式、路径就丢了
    IUnknown* streamProbe = nullptr;
    hr = preview->QueryInterface(IID_IInitializeWithStream,
                                 reinterpret_cast<void**>(&streamProbe));
    Step(L"QI IInitializeWithStream (expect E_NOINTERFACE) hr=0x%08lX",
         static_cast<unsigned long>(hr));
    if (streamProbe) streamProbe->Release();
    if (SUCCEEDED(hr)) {
        Step(L"FAIL: IInitializeWithStream must not be exposed");
        preview->Release();
        fileInit->Release();
        return 1;
    }

    HWND host = CreateHostWindow(816, 638);  // 客户区约 800x600
    ShowWindow(host, SW_SHOW);
    RECT rect = {8, 8, 808, 592};
    RECT smaller = rect;
    smaller.right -= 120;  // 模拟拖动窗格分隔线后的 SetRect
    smaller.bottom -= 60;

    hr = fileInit->Initialize(opt.slide.c_str(), STGM_READ);
    Step(L"IInitializeWithFile::Initialize hr=0x%08lX", static_cast<unsigned long>(hr));
    if (FAILED(hr)) {
        fileInit->Release();
        DestroyWindow(host);
        preview->Release();
        return 1;
    }

    hr = preview->SetWindow(host, &rect);
    Step(L"SetWindow hr=0x%08lX", static_cast<unsigned long>(hr));
    if (FAILED(hr)) {
        DestroyWindow(host);
        preview->Release();
        return 1;
    }

    hr = preview->SetRect(&smaller);
    Step(L"SetRect (resize) hr=0x%08lX", static_cast<unsigned long>(hr));

    const ULONGLONG tick = GetTickCount64();
    hr = preview->DoPreview();
    Step(L"DoPreview hr=0x%08lX in %lums", static_cast<unsigned long>(hr),
         static_cast<unsigned long>(GetTickCount64() - tick));
    if (FAILED(hr)) {
        DestroyWindow(host);
        preview->Release();
        return 1;
    }

    PumpMessages(300);  // 让 WM_PAINT 真正跑一遍

    IOleWindow* oleWindow = nullptr;
    hr = preview->QueryInterface(IID_PPV_ARGS(&oleWindow));
    HWND previewWindow = nullptr;
    if (SUCCEEDED(hr)) {
        hr = oleWindow->GetWindow(&previewWindow);
        oleWindow->Release();
    }
    Step(L"IOleWindow::GetWindow hr=0x%08lX hwnd=%p", static_cast<unsigned long>(hr),
         previewWindow);
    if (FAILED(hr) || !previewWindow) {
        DestroyWindow(host);
        preview->Release();
        return 1;
    }

    // 抓客户区存 PNG（PrintWindow 走 WM_PRINTCLIENT，可重复用于 resize 后二次抓帧）
    auto captureToPng = [&](const std::wstring& path) -> bool {
        RECT rc = {};
        GetClientRect(previewWindow, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        HDC screen = GetDC(nullptr);
        HDC memory = CreateCompatibleDC(screen);
        HBITMAP shot = CreateCompatibleBitmap(screen, w, h);
        HBITMAP oldShot = static_cast<HBITMAP>(SelectObject(memory, shot));
        const BOOL printed = PrintWindow(previewWindow, memory, PW_CLIENTONLY);
        SelectObject(memory, oldShot);
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        if (!printed) {
            Step(L"FAIL: PrintWindow (%dx%d)", w, h);
            DeleteObject(shot);
            return false;
        }
        const HRESULT pngHr = SaveBitmapToPng(shot, path);
        DeleteObject(shot);
        Step(L"PrintWindow OK (%dx%d), save %s hr=0x%08lX", w, h, path.c_str(),
             static_cast<unsigned long>(pngHr));
        return SUCCEEDED(pngHr);
    };

    int code = 0;
    if (!captureToPng(opt.outPng)) code = 1;

    // 冒烟：焦点/按键转发（无 site 时 TranslateAccelerator 预期 S_FALSE）
    const HRESULT focusHr = preview->SetFocus();
    HWND focused = nullptr;
    preview->QueryFocus(&focused);
    MSG dummy = {};
    dummy.message = WM_KEYDOWN;
    dummy.wParam = VK_TAB;
    const HRESULT accelHr = preview->TranslateAccelerator(&dummy);
    Step(L"SetFocus hr=0x%08lX QueryFocus hwnd=%p TranslateAccelerator hr=0x%08lX",
         static_cast<unsigned long>(focusHr), focused, static_cast<unsigned long>(accelHr));

    // resize 阶段：SetRect 放大超阈值 → 250ms 防抖 → 会话内重取更匹配的图层
    RECT large = {8, 8, 1508, 1108};
    hr = preview->SetRect(&large);
    Step(L"SetRect (enlarge, triggers debounce refetch) hr=0x%08lX",
         static_cast<unsigned long>(hr));
    const ULONGLONG tick3 = GetTickCount64();
    PumpMessages(700);  // 覆盖防抖定时器 + 会话重取 + 重绘
    Step(L"resize refetch (incl. 250ms debounce) in %lums",
         static_cast<unsigned long>(GetTickCount64() - tick3));
    if (!captureToPng(opt.outPng + L".resized.png")) code = 1;

    // 复用契约：Unload 后同实例可重新 Initialize + DoPreview
    hr = preview->Unload();
    Step(L"Unload hr=0x%08lX", static_cast<unsigned long>(hr));
    hr = fileInit->Initialize(opt.slide.c_str(), STGM_READ);
    const ULONGLONG tick2 = GetTickCount64();
    const HRESULT redoHr = SUCCEEDED(hr) ? preview->DoPreview() : E_FAIL;
    Step(L"re-Initialize + DoPreview hr=0x%08lX in %lums", static_cast<unsigned long>(redoHr),
         static_cast<unsigned long>(GetTickCount64() - tick2));
    if (FAILED(hr) || FAILED(redoHr)) code = 1;
    preview->Unload();
    preview->Release();
    fileInit->Release();
    DestroyWindow(host);
    return code;
}

int RunPropsFlow(DllGetClassObjectFn getClassObject, const Options& opt) {
    IClassFactory* factory = nullptr;
    HRESULT hr = getClassObject(CLSID_SlidePropertyStore, IID_PPV_ARGS(&factory));
    Step(L"DllGetClassObject(CLSID_SlidePropertyStore) hr=0x%08lX", static_cast<unsigned long>(hr));
    if (FAILED(hr)) return 1;

    IInitializeWithFile* fileInit = nullptr;
    hr = factory->CreateInstance(nullptr, IID_PPV_ARGS(&fileInit));
    Step(L"CreateInstance -> IInitializeWithFile hr=0x%08lX", static_cast<unsigned long>(hr));
    factory->Release();
    if (FAILED(hr)) return 1;

    IPropertyStore* store = nullptr;
    hr = fileInit->QueryInterface(IID_PPV_ARGS(&store));
    Step(L"QI IPropertyStore hr=0x%08lX", static_cast<unsigned long>(hr));
    if (FAILED(hr)) {
        fileInit->Release();
        return 1;
    }
    IPropertyStoreCapabilities* capabilities = nullptr;
    hr = store->QueryInterface(IID_PPV_ARGS(&capabilities));
    Step(L"QI IPropertyStoreCapabilities hr=0x%08lX", static_cast<unsigned long>(hr));
    if (FAILED(hr)) {
        store->Release();
        fileInit->Release();
        return 1;
    }

    // QI 契约反向验证：Stream/Item 必须不可达，否则 Shell 会改走流式/项式、路径就丢了
    int code = 0;
    for (const IID& probe : {IID_IInitializeWithStream, IID_IInitializeWithItem}) {
        IUnknown* sink = nullptr;
        const HRESULT probeHr = store->QueryInterface(probe, reinterpret_cast<void**>(&sink));
        if (sink) sink->Release();
        Step(L"QI %s (expect E_NOINTERFACE) hr=0x%08lX",
             probe == IID_IInitializeWithStream ? L"IInitializeWithStream" : L"IInitializeWithItem",
             static_cast<unsigned long>(probeHr));
        if (SUCCEEDED(probeHr)) {
            Step(L"FAIL: %s must not be exposed",
                 probe == IID_IInitializeWithStream ? L"IInitializeWithStream" : L"IInitializeWithItem");
            code = 1;
        }
    }
    if (code != 0) {
        capabilities->Release();
        store->Release();
        fileInit->Release();
        return code;
    }

    hr = fileInit->Initialize(opt.slide.c_str(), STGM_READ);
    Step(L"IInitializeWithFile::Initialize hr=0x%08lX", static_cast<unsigned long>(hr));
    if (FAILED(hr)) {
        store->Release();
        fileInit->Release();
        return 1;
    }

    // 首 GetValue 即懒解析（一次 makeSlide 纯标签遍历），计时即元数据成本
    PROPVARIANT first = {};
    const ULONGLONG tick = GetTickCount64();
    hr = store->GetValue(PKEY_Image_HorizontalSize, &first);
    Step(L"first GetValue (lazy parse) hr=0x%08lX in %lums", static_cast<unsigned long>(hr),
         static_cast<unsigned long>(GetTickCount64() - tick));
    PropVariantClear(&first);
    if (FAILED(hr)) {
        capabilities->Release();
        store->Release();
        fileInit->Release();
        return 1;
    }

    DWORD count = 0;
    hr = store->GetCount(&count);
    Step(L"GetCount hr=0x%08lX count=%lu", static_cast<unsigned long>(hr), count);
    for (DWORD i = 0; i < count; ++i) {
        PROPERTYKEY key = {};
        PROPVARIANT value = {};
        if (FAILED(store->GetAt(i, &key)) || FAILED(store->GetValue(key, &value))) {
            Step(L"FAIL: GetAt/GetValue at %lu", i);
            code = 1;
            break;
        }
        wchar_t text[256] = L"";
        PSFormatForDisplay(key, value, PDFF_DEFAULT, text, ARRAYSIZE(text));
        wchar_t fmtid[64] = L"";
        StringFromGUID2(key.fmtid, fmtid, ARRAYSIZE(fmtid));
        Step(L"[%lu] %s pid=%u = %s (vt=0x%04X)", i, fmtid, key.pid, text, value.vt);
        PropVariantClear(&value);
    }
    if (count == 0) {
        Step(L"WARN: no properties (parse failed or unknown format)");
        code = 1;
    }

    // 未提供键一律 VT_EMPTY（解析失败时全部如此）
    PROPVARIANT absent = {};
    hr = store->GetValue(PKEY_Image_Compression, &absent);
    Step(L"GetValue(PKEY_Image_Compression) hr=0x%08lX vt=0x%04X (expect VT_EMPTY)",
         static_cast<unsigned long>(hr), absent.vt);
    if (absent.vt != VT_EMPTY) {
        Step(L"FAIL: absent key must be VT_EMPTY");
        code = 1;
    }
    PropVariantClear(&absent);

    // 只读契约
    PROPVARIANT write = {};
    write.vt = VT_UI4;
    write.uintVal = 0;
    const HRESULT setHr = store->SetValue(PKEY_Image_HorizontalSize, write);
    const HRESULT commitHr = store->Commit();
    const HRESULT writableHr = capabilities->IsPropertyWritable(PKEY_Image_HorizontalSize);
    Step(L"SetValue hr=0x%08lX Commit hr=0x%08lX IsPropertyWritable hr=0x%08lX (expect fail/fail/S_FALSE)",
         static_cast<unsigned long>(setHr), static_cast<unsigned long>(commitHr),
         static_cast<unsigned long>(writableHr));
    if (SUCCEEDED(setHr) || SUCCEEDED(commitHr) || writableHr != S_FALSE) {
        Step(L"FAIL: store must be read-only");
        code = 1;
    }

    // 复用契约：重新 Initialize 后可再查（缓存随 Initialize 重置）
    hr = fileInit->Initialize(opt.slide.c_str(), STGM_READ);
    PROPVARIANT again = {};
    const HRESULT againHr = SUCCEEDED(hr) ? store->GetValue(PKEY_Image_HorizontalSize, &again) : E_FAIL;
    Step(L"re-Initialize + GetValue hr=0x%08lX vt=0x%04X", static_cast<unsigned long>(againHr),
         again.vt);
    if (FAILED(hr) || FAILED(againHr)) code = 1;
    PropVariantClear(&again);

    capabilities->Release();
    store->Release();
    fileInit->Release();
    return code;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    // 宽输出走 UTF-8：Git Bash / Windows Terminal 直读，conhost 配 65001 也正确
    _setmode(_fileno(stdout), _O_U8TEXT);
    SetConsoleOutputCP(CP_UTF8);

    // 对齐 Explorer 宿主的 PerMonitorV2：预览子窗按物理像素建窗后，unaware 的替身
    // 宿主会在高 DPI 屏上被 DWM 反向放大，混挂出错误布局
    if (const auto setCtx = reinterpret_cast<HANDLE (WINAPI*)(HANDLE)>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")))
        setCtx(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));

    const Options opt = ParseOptions(argc, argv);
    const ULONGLONG startTick = GetTickCount64();

    Step(L"slide: %s (mode=%ls size=%u)", opt.slide.c_str(),
         opt.preview ? L"preview" : opt.props ? L"props" : L"thumbnail", opt.size);

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

    const auto getClassObject = reinterpret_cast<DllGetClassObjectFn>(
        GetProcAddress(dll, "DllGetClassObject"));
    if (!getClassObject) {
        Step(L"FAIL: DllGetClassObject not exported");
        return 1;
    }

    const int code = opt.preview ? RunPreviewFlow(getClassObject, opt)
                                 : opt.props ? RunPropsFlow(getClassObject, opt)
                                             : RunThumbnailFlow(getClassObject, opt);

    if (code == 0) {
        const auto canUnload = reinterpret_cast<HRESULT (STDMETHODCALLTYPE*)()>(
            GetProcAddress(dll, "DllCanUnloadNow"));
        if (canUnload) {
            Step(L"DllCanUnloadNow hr=0x%08lX", static_cast<unsigned long>(canUnload()));
        }
        Step(L"PASS in %.1fs", static_cast<double>(GetTickCount64() - startTick) / 1000.0);
    }

    CoUninitialize();
    return code;
}
