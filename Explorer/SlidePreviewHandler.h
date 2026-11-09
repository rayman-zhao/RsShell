#pragma once
#include <ocidl.h>    // IObjectWithSite
#include <propsys.h>  // IInitializeWithFile
#include <shobjidl.h>
#include <string>

// {BC9E79FA-7839-44DB-BC76-2DBBB97C44C8}
constexpr GUID CLSID_SlidePreviewHandler = {
    0xbc9e79fa, 0x7839, 0x44db, {0xbc, 0x76, 0x2d, 0xbb, 0xb9, 0x7c, 0x44, 0xc8}};

// 数字切片预览窗格：IPreviewHandler 垫片。解码与缩略图同路走 RsSlidePreviewBitmap
// 宏图直出（实测 15-266ms，同步 DoPreview 预算内），本类只做 Shell 窗口契约翻译。
// 初始化接口刻意只暴露 IInitializeWithFile/IInitializeWithItem——QI 勿加回
// IInitializeWithStream：Shell 见 Stream 必选流式（官方文档/Toub 语义），而 GB 级
// 切片与闭源厂商 DLL 只能按路径打开；缩略图侧隐藏 IThumbnailProvider 是同一教训。
// Initialize 只存路径，DoPreview 才解码（官方要求）。
class SlidePreviewHandler final : public IPreviewHandler,
                                  public IOleWindow,
                                  public IObjectWithSite,
                                  public IInitializeWithFile,
                                  public IInitializeWithItem,
                                  public IPreviewHandlerVisuals {
  public:
    // m_ref=1 是创建者的初始引用，对应的模块锁在构造里配平（Release 到 0 才释放）
    SlidePreviewHandler();

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP SetWindow(HWND hwnd, const RECT* prc) override;
    STDMETHODIMP SetRect(const RECT* prc) override;
    STDMETHODIMP DoPreview() override;
    STDMETHODIMP Unload() override;
    STDMETHODIMP SetFocus() override;
    STDMETHODIMP QueryFocus(HWND* phwnd) override;
    STDMETHODIMP TranslateAccelerator(MSG* pmsg) override;

    STDMETHODIMP GetWindow(HWND* phwnd) override;
    STDMETHODIMP ContextSensitiveHelp(BOOL fEnterMode) override;

    STDMETHODIMP SetSite(IUnknown* pUnkSite) override;
    STDMETHODIMP GetSite(REFIID riid, void** ppvSite) override;

    STDMETHODIMP Initialize(LPCWSTR pszFilePath, DWORD grfMode) override;
    STDMETHODIMP Initialize(IShellItem* psi, DWORD grfMode) override;

    STDMETHODIMP SetBackgroundColor(COLORREF color) override;
    STDMETHODIMP SetFont(const LOGFONTW* plf) override;
    STDMETHODIMP SetTextColor(COLORREF color) override;

  private:
    ~SlidePreviewHandler();

    void ApplyBounds();
    void DestroyPreview();
    void Paint(HWND hwnd);
    void Render(HDC dc, int width, int height) const;
    static void EnsureWindowClass();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    volatile ULONG m_ref = 1;
    std::wstring m_path;
    HWND m_parent = nullptr;
    RECT m_rect = {};
    HWND m_window = nullptr;
    HBITMAP m_bitmap = nullptr;
    COLORREF m_background = RGB(0xff, 0xff, 0xff);
    IUnknown* m_site = nullptr;
    IPreviewHandlerFrame* m_frame = nullptr;
};
