#pragma once
#include <windows.h>

#include <cstddef>  // offsetof：RsSlideInfo 布局断言

// RsSlideCABI.dll（RsSlide 仓同名动态产品/目标）的跨仓 C ABI 契约——镜像其
// @_cdecl 实现（CABI.swift），改任一侧必须同步。本仓直接链接其 import 库
// （RsSlideCABI.lib，构建产物），符号在编译期解析；DLL 随消费方包平铺于包根，
// 打包进程的包图搜索负责在 surrogate 里解析静态导入。
//   RsSlidePreviewBitmap(path, maxSize, outBitmap, outWidth, outHeight)
//     宏图即字面意义的 Windows 位图栅格：32bpp BGRA（alpha 不透明）、自底向上、
//     stride = width*4（恒对齐）、缓冲 = stride*height、宏图原生分辨率；
//     maxSize 暂未用（保留）。两条宏图路都在 DLL 内收敛（像素直读转换 / 存储
//     JPEG 就地解压），仅当格式无宏图时失败
//   RsSlideThumbnailJpeg(path, maxSize, outJpeg, outSize)
//     金字塔层缩略图 JPEG（需完整解析切片，~20s 慢路）；保留导出备将来使用
//   RsSlideSessionOpen / RsSlideSessionThumbnailBitmap / RsSlideSessionClose
//     会话式金字塔取图：Open 打开切片（一次完整解析，无金字塔特例此处就慢）；
//     SessionThumbnailBitmap 按窗格尺寸取合成彩色位图——**24bpp BGR**、自底向
//     上、stride = (w*3+3)&~3（DWORD 对齐，非 4 倍宽有行尾填充）：明场 RGB 直转，
//     灰度平面（单通道=白色伪彩色的灰度呈现）逐通道按 pseudoColorRGB 加色合成
//     进结果缓冲（饱和截断；无色通道零贡献、跳过不取）；返回"刚好 ≥ maxSize"
//     金字塔层的原生分辨率（层怎么处理由本端决定：分配失败 E_OUTOFMEMORY 即
//     回退宏图），最终缩放由绘制端 StretchBlt 完成，多次调用只做取层+合成
//     （顶层像素已缓存在会话内）；Close 释放会话（TIFF 句柄随之关闭，预览期
//     间文件被只读持有的窗口 = Open→Close）
//   RsSlideInfo(path, outInfo)
//     切片技术元数据（属性处理器用，一次 makeSlide 纯标签遍历、不解码像素）：
//     单块 CoTaskMem 缓冲 = RsSlideInfo 定长头，纯数值字段。mpp/objective 为
//     0 表示未知；tileCompression（0=jpeg 1=png 2=jxl，镜像 CompressionHint
//     声明序）与 tilePixelFormat（1=gray 3=rgb，即 PixelFormatHint.rawValue=
//     通道数）供消费侧重建 TileTrait.description；bitDepth/levelCount/tierCount
//     只随结构携带、不映射属性键（无现成 PKEY，备将来 10bit/自定义 propdesc，
//     免跨仓改 ABI）；keywordCount 保留 ABI 槽位、恒 0——channelTrait 默认染色
//     名（H&E）不完全正确，无可靠染色来源不输出（将来启用时也不得掺
//     ImageDescription 原文，内含患者信息）。
// 缓冲一律由 RsSlideCABI.dll 以 CoTaskMemAlloc 分配，调用方 CoTaskMemFree；
// path 为切片文件绝对路径（调用期间有效即可）；Swift 侧 SIZE_T 以 Int 镜像。
// 头布局由 Info.swift 的字节偏移写入定死（0/4/8/12/16/24/32/36/40/44/48/52/56），
// 断言冻结。
struct SlideInfo {
    UINT32 width;         // 基底层像素宽
    UINT32 height;
    UINT32 bitDepth;
    UINT32 levelCount;
    double mpp;           // µm/px，0=未知
    double objective;     // 物镜倍率，0=未知
    UINT32 keywordCount;  // 保留槽位，恒 0
    UINT32 tierCount;
    UINT32 tileWidth;
    UINT32 tileHeight;
    UINT32 tileCompression;  // 0=jpeg 1=png 2=jxl
    UINT32 tilePixelFormat;  // 1=gray 3=rgb（通道数）
    UINT32 reserved;
};
static_assert(offsetof(SlideInfo, mpp) == 16, "布局与 Info.swift 契约绑定");
static_assert(offsetof(SlideInfo, objective) == 24, "布局与 Info.swift 契约绑定");
static_assert(offsetof(SlideInfo, keywordCount) == 32, "布局与 Info.swift 契约绑定");
static_assert(offsetof(SlideInfo, tierCount) == 36, "布局与 Info.swift 契约绑定");
static_assert(offsetof(SlideInfo, tileWidth) == 40, "布局与 Info.swift 契约绑定");
static_assert(offsetof(SlideInfo, tileHeight) == 44, "布局与 Info.swift 契约绑定");
static_assert(offsetof(SlideInfo, tileCompression) == 48, "布局与 Info.swift 契约绑定");
static_assert(offsetof(SlideInfo, tilePixelFormat) == 52, "布局与 Info.swift 契约绑定");
static_assert(sizeof(SlideInfo) == 64, "布局与 Info.swift 契约绑定");

extern "C" {
__declspec(dllimport) HRESULT RsSlidePreviewBitmap(PCWSTR path, UINT32 maxSize,
                                                   BYTE** outBitmap, UINT32* outWidth,
                                                   UINT32* outHeight);
__declspec(dllimport) HRESULT RsSlideThumbnailJpeg(PCWSTR path, UINT32 maxSize, BYTE** outJpeg,
                                                   SIZE_T* outSize);
__declspec(dllimport) HRESULT RsSlideSessionOpen(PCWSTR path, void** outSession);
__declspec(dllimport) HRESULT RsSlideSessionThumbnailBitmap(void* session, UINT32 maxSize,
                                                            BYTE** outBitmap, UINT32* outWidth,
                                                            UINT32* outHeight);
__declspec(dllimport) void RsSlideSessionClose(void* session);
__declspec(dllimport) HRESULT RsSlideInfo(PCWSTR path, SlideInfo** outInfo);
}
