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
//   RsSlideThumbnail(path, maxSize, outJpeg, outSize)
//     金字塔层缩略图 JPEG（需完整解析切片，~20s 慢路）；保留导出备将来使用
// 缓冲一律由 RsSlideCABI.dll 以 CoTaskMemAlloc 分配，调用方 CoTaskMemFree；
// path 为切片文件绝对路径（调用期间有效即可）；Swift 侧 SIZE_T 以 Int 镜像。
extern "C" {
__declspec(dllimport) HRESULT RsSlidePreviewBitmap(PCWSTR path, UINT32 maxSize,
                                                   BYTE** outBitmap, UINT32* outWidth,
                                                   UINT32* outHeight);
__declspec(dllimport) HRESULT RsSlideThumbnail(PCWSTR path, UINT32 maxSize, BYTE** outJpeg,
                                               SIZE_T* outSize);
}
