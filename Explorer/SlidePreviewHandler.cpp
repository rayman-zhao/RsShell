#include <windows.h>

#include <algorithm>

#include "ModuleLocks.h"
#include "SlidePreviewHandler.h"
#include "SlideRaster.h"
#include "SlideShim.h"

namespace {

constexpr wchar_t kPreviewWindowClass[] = L"RsShell.SlidePreview";

HINSTANCE ModuleInstance() {
    HINSTANCE instance = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ModuleInstance), &instance);
    return instance;
}

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
    return S_OK;
}

STDMETHODIMP SlidePreviewHandler::DoPreview() {
    if (m_path.empty() || !m_parent) return E_FAIL;
    DestroyPreview();  // 宿主未按契约先 Unload 的重入兜底

    const LONG spanW = m_rect.right - m_rect.left;
    const LONG spanH = m_rect.bottom - m_rect.top;
    // maxSize 当前为 C ABI 保留参数（恒宏图原生分辨率），仅需 >0
    const UINT32 maxSize = static_cast<UINT32>(std::max<LONG>({spanW, spanH, 1}));

    BYTE* raster = nullptr;
    UINT32 width = 0;
    UINT32 height = 0;
    HRESULT hr = RsSlidePreviewBitmap(m_path.c_str(), maxSize, &raster, &width, &height);
    if (SUCCEEDED(hr) && raster && width > 0 && height > 0) {
        hr = RasterToBitmap(raster, width, height, &m_bitmap);
    } else {
        hr = E_FAIL;
    }
    CoTaskMemFree(raster);
    if (FAILED(hr)) return hr;

    EnsureWindowClass();
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
    DestroyPreview();
    m_path.clear();
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
    m_path = pszFilePath;
    return S_OK;
}

STDMETHODIMP SlidePreviewHandler::Initialize(IShellItem* psi, DWORD) {
    if (!psi) return E_INVALIDARG;
    PWSTR path = nullptr;
    HRESULT hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &path);
    if (FAILED(hr) || !path) return FAILED(hr) ? hr : E_FAIL;
    m_path = path;
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
