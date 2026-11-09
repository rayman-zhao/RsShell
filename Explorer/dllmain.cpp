// RsShell.Explorer.dll — Explorer 上下文菜单与缩略图 COM 宿主。
// 被包内 surrogate（dllhost）加载，不由 explorer.exe 直接加载。
#include <windows.h>

#include <new>

#include "OpenInAppCommand.h"
#include "SlideThumbnailProvider.h"

static volatile LONG g_moduleLocks = 0;

void ModuleLockAdd() noexcept { InterlockedIncrement(&g_moduleLocks); }
void ModuleLockRelease() noexcept { InterlockedDecrement(&g_moduleLocks); }

namespace {

// new + QI 到 riid：接口指针校正由 QI 完成，失败路径统一走 Release
template <typename T>
HRESULT CreateComObject(REFIID riid, void** ppv) noexcept {
    auto* object = new (std::nothrow) T();
    if (!object) return E_OUTOFMEMORY;
    const HRESULT hr = object->QueryInterface(riid, ppv);
    object->Release();
    return hr;
}

// 模块生命周期单例：AddRef/Release 只计数不释放，DllCanUnloadNow 恒持活
class Factory final : public IClassFactory {
  public:
    explicit Factory(HRESULT (*create)(REFIID, void**)) noexcept : m_create(create) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    STDMETHODIMP LockServer(BOOL lock) override {
        lock ? ModuleLockAdd() : ModuleLockRelease();
        return S_OK;
    }

    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (outer) return CLASS_E_NOAGGREGATION;
        return m_create(riid, ppv);
    }

  private:
    HRESULT (*m_create)(REFIID, void**);
};

Factory g_openInAppFactory{&CreateComObject<OpenInAppCommand>};
Factory g_slideThumbnailFactory{&CreateComObject<SlideThumbnailProvider>};

}  // namespace

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    Factory* factory = nullptr;
    if (IsEqualCLSID(rclsid, CLSID_OpenInAppCommand)) {
        factory = &g_openInAppFactory;
    } else if (IsEqualCLSID(rclsid, CLSID_SlideThumbnailProvider)) {
        factory = &g_slideThumbnailFactory;
    } else {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    return factory->QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() { return g_moduleLocks == 0 ? S_OK : S_FALSE; }

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
