#pragma once
#include <windows.h>

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
// 缓冲一律由 RsSlideCABI.dll 以 CoTaskMemAlloc 分配，调用方 CoTaskMemFree；
// path 为切片文件绝对路径（调用期间有效即可）；Swift 侧 SIZE_T 以 Int 镜像。
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
}
