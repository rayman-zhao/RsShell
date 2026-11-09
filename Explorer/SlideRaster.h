#pragma once
#include <windows.h>

// RsSlidePreviewBitmap 的输出即 32bpp BGRA 自底向上栅格（stride = w*4），
// 与正高 DIB 的像素布局完全一致——直接灌入 CreateDIBSection，零像素后处理。
// 缩略图（IExtractImage）与预览窗格（IPreviewHandler）共用。
inline HRESULT RasterToBitmap(const BYTE* raster, UINT32 width, UINT32 height, HBITMAP* out) {
    *out = nullptr;
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = static_cast<LONG>(height);  // 正高 = 自底向上，与栅格一致
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) return E_FAIL;
    memcpy(bits, raster, static_cast<SIZE_T>(width) * height * 4);
    *out = bitmap;
    return S_OK;
}
