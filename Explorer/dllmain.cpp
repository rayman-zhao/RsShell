// RsShell.Explorer.dll — Explorer 上下文菜单 COM 宿主。
// 被包内 surrogate（dllhost）加载，不由 explorer.exe 直接加载。
#include <windows.h>

#include <new>

#include "OpenInAppCommand.h"

static volatile LONG g_moduleLocks = 0;

void ModuleLockAdd() noexcept { InterlockedIncrement(&g_moduleLocks); }
void ModuleLockRelease() noexcept { InterlockedDecrement(&g_moduleLocks); }

namespace {

// 模块生命周期单例：AddRef/Release 只计数不释放，DllCanUnloadNow 恒持活
class Factory final : public IClassFactory {
  public:
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
        auto* command = new (std::nothrow) OpenInAppCommand();
        if (!command) return E_OUTOFMEMORY;
        const HRESULT hr = command->QueryInterface(riid, ppv);
        command->Release();
        return hr;
    }
};

Factory g_factory;

}  // namespace

STDAPI DllGetClassFactory(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (!IsEqualCLSID(rclsid, CLSID_OpenInAppCommand)) return CLASS_E_CLASSNOTAVAILABLE;
    return g_factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() { return g_moduleLocks == 0 ? S_OK : S_FALSE; }

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
