#include <windows.h>

#include <algorithm>

#include "ModuleLocks.h"
#include "SlidePreviewHandler.h"
#include "SlideRaster.h"
#include "SlideShim.h"

namespace {

constexpr wchar_t kPreviewWindowClass[] = L"RsShell.SlidePreview";
constexpr UINT_PTR kResizeRefetchTimer = 1;
constexpr UINT kResizeRefetchDelay = 250;  // ms，拖动窗格分隔线时合并 SetRect 风暴

HINSTANCE ModuleInstance() {
    HINSTANCE instance = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ModuleInstance), &instance);
    return instance;
}

// COM surrogate 默认 DPI-unaware，而宿主 Explorer（PerMonitorV2）传入的 SetRect 是
// 物理像素：200% 缩放实机上 unaware 子窗的画面被 DWM 整体放大 2 倍，恰好只剩窗格
// 左上四分之一可见。窗口的 DPI 上下文在创建瞬间取自创建线程，故此处须临时切换。
HANDLE (WINAPI* g_setThreadDpiContext)(HANDLE) = [] {
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    return user32 ? reinterpret_cast<HANDLE (WINAPI*)(HANDLE)>(
                        GetProcAddress(user32, "SetThreadDpiAwarenessContext"))
                  : nullptr;
}();
const HANDLE kPerMonitorAwareV2 = reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4));

class ThreadDpiScope {
   public:
    ThreadDpiScope() {
        if (g_setThreadDpiContext) restore_ = g_setThreadDpiContext(kPerMonitorAwareV2);
    }
    ~ThreadDpiScope() {
        if (restore_) g_setThreadDpiContext(restore_);
    }

   private:
    HANDLE restore_ = nullptr;
};

}  // namespace

STDMETHODIMP SlidePreviewHandler::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IPreviewHandler) {
        *ppv = static_cast<IPreviewHandler*>(this);
    } else if (riid == IID_IOleWindow) {
        *ppv = static_cast<IOleWindow*>(this);
    } else if (riid == IID_IObjectWithSite) {
        *ppv = static_cast<IObjectWithSite*>(this);
    } else if (riid == IID_IInitializeWithFile) {
        *ppv = static_cast<IInitializeWithFile*>(this);
    } else if (riid == IID_IInitializeWithItem) {
        *ppv = static_cast<IInitializeWithItem*>(this);
    } else if (riid == IID_IPreviewHandlerVisuals) {
        *ppv = static_cast<IPreviewHandlerVisuals*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) SlidePreviewHandler::AddRef() {
    const ULONG ref = InterlockedIncrement(&m_ref);
    if (ref == 1) ModuleLockAdd();
    return ref;
}

STDMETHODIMP_(ULONG) SlidePreviewHandler::Release() {
    const ULONG ref = InterlockedDecrement(&m_ref);
    if (ref == 0) {
        ModuleLockRelease();
        delete this;
    }
    return ref;
}

SlidePreviewHandler::SlidePreviewHandler() { ModuleLockAdd(); }

SlidePreviewHandler::~SlidePreviewHandler() {
    DestroyPreview();
    CloseRasterSession();
    if (m_site) m_site->Release();
    if (m_frame) m_frame->Release();
}

STDMETHODIMP SlidePreviewHandler::SetWindow(HWND hwnd, const RECT* prc) {
    if (!hwnd || !prc) return E_INVALIDARG;
    m_parent = hwnd;
    m_rect = *prc;
    if (m_window) {
        SetParent(m_window, hwnd);
        ApplyBounds();
    }
    return S_OK;
}

STDMETHODIMP SlidePreviewHandler::SetRect(const RECT* prc) {
    if (!prc) return E_INVALIDARG;
    m_rect = *prc;
    ApplyBounds();
    // 尺寸显著变化（±25%）时防抖重取更匹配的图层；期间旧位图按新窗格拉伸占位
    const UINT target = PaneMaxSize();
    if (m_window && m_fetchedMaxSize > 0
        && (target * 4 > m_fetchedMaxSize * 5 || m_fetchedMaxSize * 4 > target * 5)) {
        SetTimer(m_window, kResizeRefetchTimer, kResizeRefetchDelay, nullptr);
    }
    return S_OK;
}

STDMETHODIMP SlidePreviewHandler::DoPreview() {
    if (m_path.empty() || !m_parent) return E_FAIL;
    DestroyPreview();  // 宿主未按契约先 Unload 的重入兜底

    HRESULT hr = FetchPreviewBitmap(PaneMaxSize());
    if (FAILED(hr)) return hr;

    const LONG spanW = m_rect.right - m_rect.left;
    const LONG spanH = m_rect.bottom - m_rect.top;
    EnsureWindowClass();
    ThreadDpiScope dpi;  // 窗口 DPI 上下文取自创建线程，须为物理像素单位
    m_window = CreateWindowExW(0, kPreviewWindowClass, L"", WS_CHILD | WS_VISIBLE,
                               m_rect.left, m_rect.top, spanW, spanH, m_parent, nullptr,
                               ModuleInstance(), this);
    if (!m_window) {
        DestroyPreview();
        return E_FAIL;
    }
    return S_OK;
}

STDMETHODIMP SlidePreviewHandler::Unload() {
    DestroyPreview();  // 窗口销毁连带其定时器
    CloseRasterSession();  // TIFF 句柄随会话释放，文件锁解除
    m_path.clear();
    m_fetchedMaxSize = 0;
    return S_OK;
}

STDMETHODIMP SlidePreviewHandler::SetFocus() {
    if (!m_window) return E_FAIL;
    ::SetFocus(m_window);
    return S_OK;
}

STDMETHODIMP SlidePreviewHandler::QueryFocus(HWND* phwnd) {
    if (!phwnd) return E_POINTER;
    *phwnd = ::GetFocus();
    return *phwnd ? S_OK : E_FAIL;
}

STDMETHODIMP SlidePreviewHandler::TranslateAccelerator(MSG* pmsg) {
    if (!pmsg) return E_INVALIDARG;
    // 官方最简模型：全部转发宿主；无 site（未宿主化）时无键可转
    return m_frame ? m_frame->TranslateAccelerator(pmsg) : S_FALSE;
}

STDMETHODIMP SlidePreviewHandler::GetWindow(HWND* phwnd) {
    if (!phwnd) return E_POINTER;
    *phwnd = m_window;
    return m_window ? S_OK : E_FAIL;
}

STDMETHODIMP SlidePreviewHandler::ContextSensitiveHelp(BOOL) { return E_NOTIMPL; }

STDMETHODIMP SlidePreviewHandler::SetSite(IUnknown* pUnkSite) {
    if (m_site) {
        m_site->Release();
        m_site = nullptr;
    }
    if (m_frame) {
        m_frame->Release();
        m_frame = nullptr;
    }
    if (pUnkSite) {
        m_site = pUnkSite;
        m_site->AddRef();
        pUnkSite->QueryInterface(IID_PPV_ARGS(&m_frame));
    }
    return S_OK;
}

STDMETHODIMP SlidePreviewHandler::GetSite(REFIID riid, void** ppvSite) {
    if (!ppvSite) return E_POINTER;
    if (!m_site) {
        *ppvSite = nullptr;
        return E_FAIL;
    }
    return m_site->QueryInterface(riid, ppvSite);
}

STDMETHODIMP SlidePreviewHandler::Initialize(LPCWSTR pszFilePath, DWORD) {
    if (!pszFilePath || !*pszFilePath) return E_INVALIDARG;
    CloseRasterSession();  // 会话绑定单一切片，换文件即作废
    m_path = pszFilePath;
    m_fetchedMaxSize = 0;
    return S_OK;
}

STDMETHODIMP SlidePreviewHandler::Initialize(IShellItem* psi, DWORD) {
    if (!psi) return E_INVALIDARG;
    PWSTR path = nullptr;
    HRESULT hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &path);
    if (FAILED(hr) || !path) return FAILED(hr) ? hr : E_FAIL;
    CloseRasterSession();
    m_path = path;
    m_fetchedMaxSize = 0;
    CoTaskMemFree(path);
    return S_OK;
}

STDMETHODIMP SlidePreviewHandler::SetBackgroundColor(COLORREF color) {
    m_background = color;
    if (m_window) InvalidateRect(m_window, nullptr, FALSE);
    return S_OK;
}

// 预览只绘宏图位图，无文本
STDMETHODIMP SlidePreviewHandler::SetFont(const LOGFONTW*) { return S_OK; }

STDMETHODIMP SlidePreviewHandler::SetTextColor(COLORREF) { return S_OK; }

void SlidePreviewHandler::ApplyBounds() {
    if (!m_window) return;
    SetWindowPos(m_window, nullptr, m_rect.left, m_rect.top, m_rect.right - m_rect.left,
                 m_rect.bottom - m_rect.top, SWP_NOACTIVATE | SWP_NOZORDER);
    InvalidateRect(m_window, nullptr, FALSE);
}

void SlidePreviewHandler::DestroyPreview() {
    if (m_window) {
        DestroyWindow(m_window);
        m_window = nullptr;
    }
    if (m_bitmap) {
        DeleteObject(m_bitmap);
        m_bitmap = nullptr;
    }
}

void SlidePreviewHandler::CloseRasterSession() {
    if (m_rasterSession) {
        RsSlideSessionClose(m_rasterSession);
        m_rasterSession = nullptr;
    }
}

UINT SlidePreviewHandler::PaneMaxSize() const {
    const LONG spanW = m_rect.right - m_rect.left;
    const LONG spanH = m_rect.bottom - m_rect.top;
    return static_cast<UINT>(std::max<LONG>({spanW, spanH, 1}));
}

// 金字塔会话取图（多通道伪彩色合成在 RsSlideCABI 侧，24bpp BGR）；失败（含打开
// 失败、无金字塔只有底图的特例）回退宏图（32bpp BGRA）
HRESULT SlidePreviewHandler::FetchPreviewBitmap(UINT maxSize) {
    if (!m_rasterSession) {
        RsSlideSessionOpen(m_path.c_str(), &m_rasterSession);
    }

    BYTE* raster = nullptr;
    UINT32 width = 0;
    UINT32 height = 0;
    HRESULT hr = E_FAIL;
    WORD bitCount = 24;
    if (m_rasterSession) {
        hr = RsSlideSessionThumbnailBitmap(m_rasterSession, maxSize, &raster, &width, &height);
        if (FAILED(hr) || !raster) hr = E_FAIL;
    }
    if (FAILED(hr)) {
        raster = nullptr;
        width = 0;
        height = 0;
        hr = RsSlidePreviewBitmap(m_path.c_str(), maxSize, &raster, &width, &height);
        if (FAILED(hr) || !raster) hr = E_FAIL;
        bitCount = 32;
    }
    if (SUCCEEDED(hr)) {
        HBITMAP fresh = nullptr;
        hr = RasterToBitmap(raster, width, height, bitCount, &fresh);
        if (SUCCEEDED(hr)) {
            if (m_bitmap) DeleteObject(m_bitmap);
            m_bitmap = fresh;
            m_fetchedMaxSize = maxSize;
        }
    }
    CoTaskMemFree(raster);
    return hr;
}

void SlidePreviewHandler::RefetchForNewSize() {
    if (!m_window) return;
    if (SUCCEEDED(FetchPreviewBitmap(PaneMaxSize()))) {
        InvalidateRect(m_window, nullptr, FALSE);
    }
}

void SlidePreviewHandler::Paint(HWND hwnd) {
    PAINTSTRUCT ps = {};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client = {};
    GetClientRect(hwnd, &client);
    Render(dc, client.right, client.bottom);
    EndPaint(hwnd, &ps);
}

void SlidePreviewHandler::Render(HDC dc, int width, int height) const {
    if (width <= 0 || height <= 0) return;

    // 双缓冲：背景与拉伸一次合成，SetRect 连发时不闪烁
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP canvas = CreateCompatibleBitmap(dc, width, height);
    HBITMAP oldCanvas = static_cast<HBITMAP>(SelectObject(buffer, canvas));
    if (!oldCanvas) {
        DeleteObject(canvas);
        DeleteDC(buffer);
        return;
    }

    HBRUSH brush = CreateSolidBrush(m_background);
    RECT full = {0, 0, width, height};
    FillRect(buffer, &full, brush);
    DeleteObject(brush);

    BITMAP info = {};
    if (m_bitmap && GetObjectW(m_bitmap, sizeof(info), &info) && info.bmWidth > 0 &&
        info.bmHeight > 0) {
        // 保纵横比缩放到窗格内可容纳的最大尺寸并居中
        const double scale = std::min(static_cast<double>(width) / info.bmWidth,
                                      static_cast<double>(height) / info.bmHeight);
        const int drawWidth = std::max(1, static_cast<int>(info.bmWidth * scale));
        const int drawHeight = std::max(1, static_cast<int>(info.bmHeight * scale));
        const int x = (width - drawWidth) / 2;
        const int y = (height - drawHeight) / 2;

        HDC source = CreateCompatibleDC(dc);
        HBITMAP oldSource = static_cast<HBITMAP>(SelectObject(source, m_bitmap));
        SetStretchBltMode(buffer, HALFTONE);  // 宏图常大于窗格，缩小用高质量插值
        StretchBlt(buffer, x, y, drawWidth, drawHeight, source, 0, 0, info.bmWidth,
                   info.bmHeight, SRCCOPY);
        SelectObject(source, oldSource);
        DeleteDC(source);
    }

    BitBlt(dc, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, oldCanvas);
    DeleteObject(canvas);
    DeleteDC(buffer);
}

void SlidePreviewHandler::EnsureWindowClass() {
    static const bool registered = [] {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &SlidePreviewHandler::WndProc;
        wc.hInstance = ModuleInstance();
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;  // 背景由 WM_PAINT 双缓冲绘制
        wc.lpszClassName = kPreviewWindowClass;
        return RegisterClassExW(&wc) != 0;
    }();
    (void)registered;
}

LRESULT CALLBACK SlidePreviewHandler::WndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                              LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    auto* self = reinterpret_cast<SlidePreviewHandler*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;  // 背景并入 WM_PAINT 双缓冲
        case WM_TIMER:
            if (self && wParam == kResizeRefetchTimer) {
                KillTimer(hwnd, kResizeRefetchTimer);
                self->RefetchForNewSize();
                return 0;
            }
            break;
        case WM_PAINT:
            if (self) {
                self->Paint(hwnd);
                return 0;
            }
            break;
        case WM_PRINTCLIENT:
            if (self && wParam) {
                RECT client = {};
                GetClientRect(hwnd, &client);
                self->Render(reinterpret_cast<HDC>(wParam), client.right, client.bottom);
                return 0;
            }
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
