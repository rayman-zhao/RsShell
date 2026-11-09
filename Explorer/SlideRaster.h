#pragma once
#include <windows.h>

// RsSlideCABI 两种位图栅格布局（均自底向上、与正高 DIB 一致，直接灌入
// CreateDIBSection 零像素后处理）：宏图 32bpp BGRA（stride = w*4）、会话合成
// 24bpp BGR（stride = (w*3+3)&~3，DWORD 对齐）。缩略图与预览窗格共用。
inline HRESULT RasterToBitmap(const BYTE* raster, UINT32 width, UINT32 height, WORD bitCount,
                              HBITMAP* out) {
    *out = nullptr;
    const SIZE_T stride =
        (static_cast<SIZE_T>(width) * (bitCount / 8) + 3) & ~static_cast<SIZE_T>(3);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = static_cast<LONG>(height);  // 正高 = 自底向上，与栅格一致
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = bitCount;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) return E_FAIL;
    memcpy(bits, raster, stride * height);
    *out = bitmap;
    return S_OK;
}
