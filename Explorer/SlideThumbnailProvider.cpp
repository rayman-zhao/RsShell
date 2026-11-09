// 数字切片缩略图：IExtractImage 垫片。解码在 RsSlide（Swift DLL），本类只做
// Shell 契约翻译——IPersistFile 拿路径、调 RsSlidePreviewBitmap 得位图栅格、
// 直接造 DIB 返回（宏图路径实测 <1s，surrogate 预算内，无需旁路进程/缓存）。
#include <windows.h>

#include "ModuleLocks.h"
#include "SlideRaster.h"
#include "SlideShim.h"
#include "SlideThumbnailProvider.h"

STDMETHODIMP SlideThumbnailProvider::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IPersistFile) {
        *ppv = static_cast<IPersistFile*>(this);
    } else if (riid == IID_IExtractImage || riid == IID_IExtractImage2) {
        *ppv = static_cast<IExtractImage2*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) SlideThumbnailProvider::AddRef() {
    const ULONG ref = InterlockedIncrement(&m_ref);
    if (ref == 1) ModuleLockAdd();
    return ref;
}

STDMETHODIMP_(ULONG) SlideThumbnailProvider::Release() {
    const ULONG ref = InterlockedDecrement(&m_ref);
    if (ref == 0) {
        ModuleLockRelease();
        delete this;
    }
    return ref;
}

SlideThumbnailProvider::SlideThumbnailProvider() { ModuleLockAdd(); }

SlideThumbnailProvider::~SlideThumbnailProvider() = default;

STDMETHODIMP SlideThumbnailProvider::GetClassID(CLSID* pClassID) {
    if (!pClassID) return E_POINTER;
    *pClassID = CLSID_SlideThumbnailProvider;
    return S_OK;
}

STDMETHODIMP SlideThumbnailProvider::IsDirty() { return S_FALSE; }

STDMETHODIMP SlideThumbnailProvider::Load(LPCOLESTR pszFileName, DWORD) {
    if (!pszFileName || !*pszFileName) return E_INVALIDARG;
    m_path = pszFileName;
    return S_OK;
}

STDMETHODIMP SlideThumbnailProvider::Save(LPCOLESTR, BOOL) { return E_NOTIMPL; }

STDMETHODIMP SlideThumbnailProvider::SaveCompleted(LPCOLESTR) { return E_NOTIMPL; }

STDMETHODIMP SlideThumbnailProvider::GetCurFile(LPOLESTR* ppszFileName) {
    if (!ppszFileName) return E_POINTER;
    *ppszFileName = nullptr;
    return S_FALSE;
}

STDMETHODIMP SlideThumbnailProvider::GetLocation(LPWSTR pszPathBuffer, DWORD cch,
                                                 DWORD* /*pdwPriority*/, const SIZE* prgSize,
                                                 DWORD, DWORD* pdwFlags) {
    if (prgSize) m_size = *prgSize;
    if (pdwFlags) *pdwFlags |= IEIFLAG_ORIGSIZE;
    if (!pszPathBuffer || cch == 0) return E_FAIL;
    wcsncpy_s(pszPathBuffer, cch, m_path.c_str(), _TRUNCATE);
    return S_OK;
}

STDMETHODIMP SlideThumbnailProvider::Extract(HBITMAP* phBmpThumbnail) {
    if (!phBmpThumbnail) return E_POINTER;
    *phBmpThumbnail = nullptr;
    if (m_path.empty()) return E_FAIL;

    const UINT maxSize = m_size.cx > 0 ? static_cast<UINT>(m_size.cx) : 256;
    BYTE* raster = nullptr;
    UINT32 width = 0;
    UINT32 height = 0;
    HRESULT hr = RsSlidePreviewBitmap(m_path.c_str(), maxSize, &raster, &width, &height);
    if (SUCCEEDED(hr) && raster && width > 0 && height > 0) {
        hr = RasterToBitmap(raster, width, height, 32, phBmpThumbnail);
    } else {
        hr = E_FAIL;
    }
    CoTaskMemFree(raster);
    return hr;
}

STDMETHODIMP SlideThumbnailProvider::GetDateStamp(FILETIME* pDateStamp) {
    if (!pDateStamp) return E_POINTER;
    WIN32_FILE_ATTRIBUTE_DATA info = {};
    if (m_path.empty() ||
        !GetFileAttributesExW(m_path.c_str(), GetFileExInfoStandard, &info)) {
        return E_FAIL;
    }
    *pDateStamp = info.ftLastWriteTime;
    return S_OK;
}
