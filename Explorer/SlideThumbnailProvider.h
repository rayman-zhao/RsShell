#pragma once
#include <shobjidl.h>
#include <string>

// {C7EE17E3-3128-4FB5-BCAF-0CCC1E86ECA9}
constexpr GUID CLSID_SlideThumbnailProvider = {
    0xc7ee17e3, 0x3128, 0x4fb5, {0xbc, 0xaf, 0xc, 0xcc, 0x1e, 0x86, 0xec, 0xa9}};

// 刻意走 IExtractImage 遗留链而非 IThumbnailProvider（2026-11-07 真机验证）：
// Shell 对 out-of-proc 缩略图 handler 只用 IInitializeWithStream 初始化，流上
// IStream::Stat 只给裸文件名拿不到路径；而 GB 级切片与闭源厂商 DLL 只能按路径
// 打开。隐藏 IThumbnailProvider/IInitializeWith* 后 Shell 回退 IPersistFile::Load
// （携带完整路径）+ IExtractImage::Extract，这条路在打包 surrogate 里实测可用。
// 勿给 QI 加回 IThumbnailProvider：一旦暴露，Shell 立即改走流式且不再回退。
class SlideThumbnailProvider final : public IPersistFile, public IExtractImage2 {
  public:
    // m_ref=1 是创建者的初始引用，对应的模块锁在构造里配平（Release 到 0 才释放）
    SlideThumbnailProvider();

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP GetClassID(CLSID* pClassID) override;
    STDMETHODIMP IsDirty() override;
    STDMETHODIMP Load(LPCOLESTR pszFileName, DWORD dwMode) override;
    STDMETHODIMP Save(LPCOLESTR pszFileName, BOOL fRemember) override;
    STDMETHODIMP SaveCompleted(LPCOLESTR pszFileName) override;
    STDMETHODIMP GetCurFile(LPOLESTR* ppszFileName) override;

    STDMETHODIMP GetLocation(LPWSTR pszPathBuffer, DWORD cch, DWORD* pdwPriority,
                             const SIZE* prgSize, DWORD dwRecClrDepth,
                             DWORD* pdwFlags) override;
    STDMETHODIMP Extract(HBITMAP* phBmpThumbnail) override;
    STDMETHODIMP GetDateStamp(FILETIME* pDateStamp) override;

  private:
    ~SlideThumbnailProvider();

    volatile ULONG m_ref = 1;
    std::wstring m_path;
    SIZE m_size = {};
};
