#pragma once
#include <propsys.h>  // IPropertyStore/IPropertyStoreCapabilities/IInitializeWithFile

#include <string>
#include <vector>

struct SlideInfo;

// {5E1D9A34-7C48-4B6A-9E2F-1D3C8A50B7E6}
constexpr GUID CLSID_SlidePropertyStore = {
    0x5e1d9a34, 0x7c48, 0x4b6a, {0x9e, 0x2f, 0x1d, 0x3c, 0x8a, 0x50, 0xb7, 0xe6}};

// 数字切片属性处理器：IPropertyStore 垫片。元数据在 RsSlideInfo（一次
// makeSlide 纯标签遍历），本类只做 Shell 属性系统契约翻译——Initialize 只存
// 路径，首次 GetValue 才懒解析并缓存（失败也缓存，退化为全 VT_EMPTY，基础
// 文件属性不受影响）；只读。映射面刻意收窄：尺寸/分辨率，外加备注摘要一行
// （TileTrait.description · 瓦片尺寸 · tier 数 · 层数 · 倍率，无专用键的技术
// 字段全塞 System.Comment）；染色关键词无可靠来源（channelTrait 默认 H&E 不
// 完全正确）暂不输出。
// 初始化只暴露 IInitializeWithFile——与缩略图隐藏 IThumbnailProvider、预览
// 隐藏 IInitializeWithStream 同一纪律：Shell 见 Stream/Item 必改道，GB 级切片
// 只能按路径打开。
class SlidePropertyStore final : public IPropertyStore,
                                 public IPropertyStoreCapabilities,
                                 public IInitializeWithFile {
  public:
    // m_ref=1 是创建者的初始引用，对应的模块锁在构造里配平（Release 到 0 才释放）
    SlidePropertyStore();

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP GetCount(DWORD* cProps) override;
    STDMETHODIMP GetAt(DWORD iProp, PROPERTYKEY* pkey) override;
    STDMETHODIMP GetValue(REFPROPERTYKEY key, PROPVARIANT* pv) override;
    STDMETHODIMP SetValue(REFPROPERTYKEY key, REFPROPVARIANT propvar) override;
    STDMETHODIMP Commit() override;

    STDMETHODIMP IsPropertyWritable(REFPROPERTYKEY key) override;

    STDMETHODIMP Initialize(LPCWSTR pszFilePath, DWORD grfMode) override;

  private:
    ~SlidePropertyStore();

    void EnsureInfo();

    volatile ULONG m_ref = 1;
    std::wstring m_path;
    // 懒解析与 Initialize 重置的互斥；懒解析后 m_info/m_keys/m_comment 只读
    SRWLOCK m_lock = SRWLOCK_INIT;
    SlideInfo* m_info = nullptr;
    bool m_infoLoaded = false;
    std::vector<PROPERTYKEY> m_keys;
    std::wstring m_comment;  // 备注摘要行，空=不出该键
};
