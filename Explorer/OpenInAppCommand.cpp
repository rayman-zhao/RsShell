// "在应用中打开"verb：枚举选中项 → 拼引号安全的命令行 → 经 AUMID 激活主应用。
// 薄翻译层，无业务逻辑；语义路由在主应用侧（Ruslan 激活参数解析）完成。
#include <windows.h>

#include <appmodel.h>
#include <shlwapi.h>
#include <string>

#include "OpenInAppCommand.h"
#include "ResourceIds.h"

namespace {

HMODULE SelfModule() noexcept {
    HMODULE module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&SelfModule), &module);
    return module;
}

HRESULT LoadResourceString(UINT id, LPWSTR* out) {
    wchar_t buffer[256] = {};
    const int length = LoadStringW(SelfModule(), id, buffer, ARRAYSIZE(buffer));
    if (length <= 0) return E_FAIL;
    return SHStrDupW(buffer, out);
}

// CommandLineToArgvW 的 CRT 规则：参数内的反斜杠只在紧邻引号时才要翻倍，
// 结尾的反斜杠串必须翻倍以免转义闭合引号（盘根 "Z:\" 是合法选中形态）
std::wstring QuoteArg(const std::wstring& arg) {
    std::wstring quoted(1, L'"');
    size_t backslashes = 0;
    for (const wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            quoted += ch;
        } else if (ch == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted += ch;
            backslashes = 0;
        } else {
            backslashes = 0;
            quoted += ch;
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted += L'"';
    return quoted;
}

bool AllItemsResolveToPaths(IShellItemArray* items) {
    if (!items) return true;
    DWORD count = 0;
    if (FAILED(items->GetCount(&count))) return false;
    if (count == 0) return true;
    for (DWORD i = 0; i < count; ++i) {
        IShellItem* item = nullptr;
        if (FAILED(items->GetItemAt(i, &item))) return false;
        LPWSTR path = nullptr;
        const HRESULT hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
        CoTaskMemFree(path);
        item->Release();
        if (FAILED(hr) || !path) return false;
    }
    return true;
}

std::wstring BuildArguments(IShellItemArray* items) {
    std::wstring args;
    if (!items) return args;
    DWORD count = 0;
    if (FAILED(items->GetCount(&count))) return args;
    for (DWORD i = 0; i < count; ++i) {
        IShellItem* item = nullptr;
        if (FAILED(items->GetItemAt(i, &item))) continue;
        LPWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path && *path) {
            if (!args.empty()) args += L' ';
            args += QuoteArg(path);
        }
        CoTaskMemFree(path);
        item->Release();
    }
    return args;
}

}  // namespace

STDMETHODIMP OpenInAppCommand::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IExplorerCommand) {
        *ppv = static_cast<IExplorerCommand*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) OpenInAppCommand::AddRef() {
    const ULONG ref = InterlockedIncrement(&m_ref);
    if (ref == 1) ModuleLockAdd();
    return ref;
}

STDMETHODIMP_(ULONG) OpenInAppCommand::Release() {
    const ULONG ref = InterlockedDecrement(&m_ref);
    if (ref == 0) {
        ModuleLockRelease();
        delete this;
    }
    return ref;
}

STDMETHODIMP OpenInAppCommand::GetTitle(IShellItemArray*, LPWSTR* ppszName) {
    if (!ppszName) return E_POINTER;
    *ppszName = nullptr;
    return LoadResourceString(IDS_CMD_TITLE, ppszName);
}

STDMETHODIMP OpenInAppCommand::GetIcon(IShellItemArray*, LPWSTR* ppszIcon) {
    if (!ppszIcon) return E_POINTER;
    *ppszIcon = nullptr;
    wchar_t modulePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(SelfModule(), modulePath, ARRAYSIZE(modulePath))) {
        return E_FAIL;
    }
    const std::wstring icon = std::wstring(modulePath) + L",-" + std::to_wstring(IDI_APP);
    return SHStrDupW(icon.c_str(), ppszIcon);
}

STDMETHODIMP OpenInAppCommand::GetToolTip(IShellItemArray* items, LPWSTR* ppszInfotip) {
    return GetTitle(items, ppszInfotip);
}

STDMETHODIMP OpenInAppCommand::GetCanonicalName(GUID* pguidCommandName) {
    if (!pguidCommandName) return E_POINTER;
    // {0149DF2A-6FEB-4300-B84A-9295D91AF54A}
    *pguidCommandName = {0x149df2a, 0x6feb, 0x4300, {0xb8, 0x4a, 0x92, 0x95, 0xd9, 0x1a, 0xf5, 0x4a}};
    return S_OK;
}

STDMETHODIMP OpenInAppCommand::GetState(IShellItemArray* psiItemArray, BOOL,
                                        EXPCMDSTATE* pCmdState) {
    if (!pCmdState) return E_POINTER;
    // 非文件系统位置（搜索结果/压缩包内/此电脑）取不到 FILESYSPATH，整组隐藏
    *pCmdState = AllItemsResolveToPaths(psiItemArray) ? ECS_ENABLED : ECS_HIDDEN;
    return S_OK;
}

STDMETHODIMP OpenInAppCommand::Invoke(IShellItemArray* psiItemArray, IBindCtx*) {
    const std::wstring args = BuildArguments(psiItemArray);
    if (args.empty()) return E_INVALIDARG;

    // PFN 运行时查询而非硬编码：debug/release 包身份、重命名都能对上；
    // AppId 段取自本 DLL 资源（Resource.rc，换应用只改那里）
    wchar_t familyName[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1] = {};
    UINT32 length = ARRAYSIZE(familyName);
    if (FAILED(GetCurrentPackageFamilyName(&length, familyName))) return E_FAIL;
    wchar_t appId[64] = {};
    if (LoadStringW(SelfModule(), IDS_TARGET_APP_ID, appId, ARRAYSIZE(appId)) <= 0) {
        return E_FAIL;
    }
    const std::wstring aumid = std::wstring(familyName) + L"!" + appId;

    IApplicationActivationManager* manager = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr,
                                  CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&manager));
    if (FAILED(hr)) return hr;

    DWORD processId = 0;
    hr = manager->ActivateApplication(aumid.c_str(), args.c_str(), AO_NONE, &processId);
    manager->Release();
    return hr;
}

STDMETHODIMP OpenInAppCommand::GetFlags(EXPCMDFLAGS* pFlags) {
    if (!pFlags) return E_POINTER;
    *pFlags = ECF_DEFAULT;
    return S_OK;
}

STDMETHODIMP OpenInAppCommand::EnumSubCommands(IEnumExplorerCommand** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = nullptr;
    return S_FALSE;
}
