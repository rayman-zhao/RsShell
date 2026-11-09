#pragma once
#include <shobjidl.h>

#include "ModuleLocks.h"

// {9183F4E9-0402-4349-BE07-1A1101ACC978}
constexpr GUID CLSID_OpenInAppCommand = {
    0x9183f4e9, 0x402, 0x4349, {0xbe, 0x7, 0x1a, 0x11, 0x1, 0xac, 0xc9, 0x78}};

class OpenInAppCommand final : public IExplorerCommand {
  public:
    // m_ref=1 是创建者的初始引用，对应的模块锁在构造里配平（Release 到 0 才释放）
    OpenInAppCommand();

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP GetTitle(IShellItemArray* psiItemArray, LPWSTR* ppszName) override;
    STDMETHODIMP GetIcon(IShellItemArray* psiItemArray, LPWSTR* ppszIcon) override;
    STDMETHODIMP GetToolTip(IShellItemArray* psiItemArray, LPWSTR* ppszInfotip) override;
    STDMETHODIMP GetCanonicalName(GUID* pguidCommandName) override;
    STDMETHODIMP GetState(IShellItemArray* psiItemArray, BOOL fOkToBeSlow,
                          EXPCMDSTATE* pCmdState) override;
    STDMETHODIMP Invoke(IShellItemArray* psiItemArray, IBindCtx* pBindCtx) override;
    STDMETHODIMP GetFlags(EXPCMDFLAGS* pFlags) override;
    STDMETHODIMP EnumSubCommands(IEnumExplorerCommand** ppEnum) override;

  private:
    volatile ULONG m_ref = 1;
};
