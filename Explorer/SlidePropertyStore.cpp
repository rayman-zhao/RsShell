#include <windows.h>

#include <propsys.h>  // 须在 propkey.h 前：PROPERTYKEY/DEFINE_PROPERTYKEY 定义于此
#include <shlwapi.h>  // SHStrDupW：LPWSTR 拷贝进 PROPVARIANT

#include <propkey.h>

#include "ModuleLocks.h"
#include "SlidePropertyStore.h"
#include "SlideShim.h"

namespace {

// 镜像 RsSlide TileTrait.description 的 switch（jxl 组合落在其 fatalError
// 分支=尚未出现，非 8bit 同理），对不上即不出摘要
const wchar_t* TileFormatText(const SlideInfo* info) {
    if (info->bitDepth != 8) return nullptr;
    const bool rgb = info->tilePixelFormat == 3;
    const bool gray = info->tilePixelFormat == 1;
    switch (info->tileCompression) {
        case 0:  // jpeg
            return rgb ? L"JPEG_RGB_24bits" : gray ? L"JPEG_GRAY_8bits" : nullptr;
        case 1:  // png
            return rgb ? L"PNG_RGB_24bits" : gray ? L"PNG_GRAY_8bits" : nullptr;
        default:
            return nullptr;
    }
}

}  // namespace

STDMETHODIMP SlidePropertyStore::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IPropertyStore) {
        *ppv = static_cast<IPropertyStore*>(this);
    } else if (riid == IID_IPropertyStoreCapabilities) {
        *ppv = static_cast<IPropertyStoreCapabilities*>(this);
    } else if (riid == IID_IInitializeWithFile) {
        *ppv = static_cast<IInitializeWithFile*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) SlidePropertyStore::AddRef() {
    const ULONG ref = InterlockedIncrement(&m_ref);
    if (ref == 1) ModuleLockAdd();
    return ref;
}

STDMETHODIMP_(ULONG) SlidePropertyStore::Release() {
    const ULONG ref = InterlockedDecrement(&m_ref);
    if (ref == 0) {
        ModuleLockRelease();
        delete this;
    }
    return ref;
}

SlidePropertyStore::SlidePropertyStore() { ModuleLockAdd(); }

SlidePropertyStore::~SlidePropertyStore() { CoTaskMemFree(m_info); }

// 懒解析：成功与失败都置位 m_infoLoaded，失败不重试（同一文件的元数据不会
// 变好，避免每次 GetValue 都付一次 makeSlide）
void SlidePropertyStore::EnsureInfo() {
    AcquireSRWLockExclusive(&m_lock);
    if (!m_infoLoaded) {
        m_infoLoaded = true;
        SlideInfo* info = nullptr;
        if (SUCCEEDED(RsSlideInfo(m_path.c_str(), &info)) && info && info->width > 0
            && info->height > 0) {
            m_info = info;
            m_keys.push_back(PKEY_Image_HorizontalSize);
            m_keys.push_back(PKEY_Image_VerticalSize);
            if (info->mpp > 0) {
                m_keys.push_back(PKEY_Image_HorizontalResolution);
                m_keys.push_back(PKEY_Image_VerticalResolution);
            }
            // 摘要顺序：倍率 · level · tier · format · 瓦片尺寸（缺省段自然跳过）。
            // 摘要值会进搜索索引：统一英文/中立符号，勿按 UI 语言分支输出——
            // 索引值会绑死建索引时的语言，切语言后旧值不刷新且跨机搜索口径不一致
            wchar_t text[96] = {};
            auto appendPart = [&text](const wchar_t* part) {
                if (text[0] != L'\0') wcscat_s(text, L" · ");
                wcscat_s(text, part);
            };
            wchar_t part[40] = {};
            if (info->objective > 0) {  // 0=未知，省略倍率段
                swprintf_s(part, L"%gx", info->objective);
                appendPart(part);
            }
            swprintf_s(part, L"level×%u", info->levelCount);
            appendPart(part);
            swprintf_s(part, L"tier×%u", info->tierCount);
            appendPart(part);
            if (const wchar_t* format = TileFormatText(info)) appendPart(format);
            swprintf_s(part, L"%ux%u", info->tileWidth, info->tileHeight);
            appendPart(part);
            m_comment = text;
            m_keys.push_back(PKEY_Comment);
        } else {
            CoTaskMemFree(info);
        }
    }
    ReleaseSRWLockExclusive(&m_lock);
}

STDMETHODIMP SlidePropertyStore::GetCount(DWORD* cProps) {
    if (!cProps) return E_POINTER;
    EnsureInfo();
    AcquireSRWLockShared(&m_lock);
    *cProps = static_cast<DWORD>(m_keys.size());
    ReleaseSRWLockShared(&m_lock);
    return S_OK;
}

STDMETHODIMP SlidePropertyStore::GetAt(DWORD iProp, PROPERTYKEY* pkey) {
    if (!pkey) return E_POINTER;
    EnsureInfo();
    AcquireSRWLockShared(&m_lock);
    HRESULT hr = E_INVALIDARG;
    if (iProp < m_keys.size()) {
        *pkey = m_keys[iProp];
        hr = S_OK;
    }
    ReleaseSRWLockShared(&m_lock);
    return hr;
}

STDMETHODIMP SlidePropertyStore::GetValue(REFPROPERTYKEY key, PROPVARIANT* pv) {
    if (!pv) return E_POINTER;
    PropVariantInit(pv);
    EnsureInfo();
    AcquireSRWLockShared(&m_lock);
    const SlideInfo* info = m_info;
    HRESULT hr = S_OK;  // 未提供键与解析失败一律 VT_EMPTY
    if (info) {
        if (IsEqualPropertyKey(key, PKEY_Image_HorizontalSize)) {
            pv->vt = VT_UI4;
            pv->uintVal = info->width;
        } else if (IsEqualPropertyKey(key, PKEY_Image_VerticalSize)) {
            pv->vt = VT_UI4;
            pv->uintVal = info->height;
        } else if (IsEqualPropertyKey(key, PKEY_Image_HorizontalResolution)
                   || IsEqualPropertyKey(key, PKEY_Image_VerticalResolution)) {
            if (info->mpp > 0) {  // µm/px → DPI
                pv->vt = VT_R8;
                pv->dblVal = 25400.0 / info->mpp;
            }
        } else if (IsEqualPropertyKey(key, PKEY_Comment) && !m_comment.empty()) {
            hr = SHStrDupW(m_comment.c_str(), &pv->pwszVal);
            if (SUCCEEDED(hr)) pv->vt = VT_LPWSTR;
        }
    }
    ReleaseSRWLockShared(&m_lock);
    return hr;
}

STDMETHODIMP SlidePropertyStore::SetValue(REFPROPERTYKEY, REFPROPVARIANT) {
    return STG_E_ACCESSDENIED;
}

STDMETHODIMP SlidePropertyStore::Commit() { return STG_E_ACCESSDENIED; }

STDMETHODIMP SlidePropertyStore::IsPropertyWritable(REFPROPERTYKEY) { return S_FALSE; }

STDMETHODIMP SlidePropertyStore::Initialize(LPCWSTR pszFilePath, DWORD) {
    if (!pszFilePath || !*pszFilePath) return E_INVALIDARG;
    AcquireSRWLockExclusive(&m_lock);
    m_path = pszFilePath;
    CoTaskMemFree(m_info);
    m_info = nullptr;
    m_infoLoaded = false;
    m_keys.clear();
    m_comment.clear();
    ReleaseSRWLockExclusive(&m_lock);
    return S_OK;
}
