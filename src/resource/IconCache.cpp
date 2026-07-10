// IconCache — 系统图标缓存实现
//
// SHGetFileInfoW → HICON → BI_RGB bitmap → RGBA pixels → ResourceManager texture

#include "resource/IconCache.hpp"
#include "resource/ResourceManager.hpp"

#include <cstdio>

namespace geofinder {

IconCache::IconCache(ResourceManager* resMgr)
    : m_resMgr(resMgr)
{
}

IconCache::~IconCache() = default;

ResourceHandle IconCache::getIcon(const std::wstring& ext, int size)
{
    IconKey key{ext, size};
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;

    // 构造临时文件路径获取系统图标
    wchar_t tmpPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpPath);
    std::wstring dummyFile = std::wstring(tmpPath) + L"geo_icon" + ext;
    // 确保文件存在（SHGetFileInfo 需要真实文件）
    HANDLE hFile = CreateFileW(dummyFile.c_str(), GENERIC_WRITE, 0,
                                nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);

    SHFILEINFOW sfi = {};
    UINT flags = SHGFI_ICON | SHGFI_LARGEICON;  // 32px 源，缩放后质量更好

    DWORD_PTR result = SHGetFileInfoW(dummyFile.c_str(), 0, &sfi, sizeof(sfi), flags);
    DeleteFileW(dummyFile.c_str());

    if (!result || !sfi.hIcon) return kInvalidHandle;

    ResourceHandle handle = extractIcon(sfi.hIcon, size);
    DestroyIcon(sfi.hIcon);

    if (handle != kInvalidHandle) m_cache[key] = handle;
    return handle;
}

ResourceHandle IconCache::getFileIcon(const std::wstring& path, int size)
{
    // 1. 路径缓存命中
    auto it = m_pathCache.find(path);
    if (it != m_pathCache.end()) return it->second;

    // 2. 尝试从实际文件提取真实图标（.exe 内嵌 / .lnk 目标）
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) {
        ResourceHandle handle = loadIconFromFile(path, size);
        if (handle != kInvalidHandle) {
            m_pathCache[path] = handle;
            return handle;
        }
    }

    // 3. 回退：按扩展名取默认类型图标
    size_t dot = path.rfind(L'.');
    std::wstring ext = (dot != std::wstring::npos) ? path.substr(dot) : L"";
    for (auto& ch : ext) ch = towlower(ch);
    if (ext.empty()) ext = L".*";
    return getIcon(ext, size);
}

ResourceHandle IconCache::loadIconFromFile(const std::wstring& path, int size)
{
    SHFILEINFOW sfi = {};
    UINT flags = SHGFI_ICON | SHGFI_LARGEICON;

    DWORD_PTR result = SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), flags);
    if (!result || !sfi.hIcon) return kInvalidHandle;

    ResourceHandle handle = extractIcon(sfi.hIcon, size);
    DestroyIcon(sfi.hIcon);
    return handle;
}

ResourceHandle IconCache::extractIcon(HICON hIcon, int size)
{
    // 获取图标信息
    ICONINFO ii;
    if (!GetIconInfo(hIcon, &ii)) return kInvalidHandle;

    // 创建兼容 DC + Bitmap
    HDC hdc = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(hdc);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBmp || !bits) {
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
        DeleteDC(memDC); ReleaseDC(nullptr, hdc);
        return kInvalidHandle;
    }

    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, hBmp);

    // 填充透明背景
    RECT rc = {0, 0, size, size};
    HBRUSH br = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(memDC, &rc, br);
    DeleteObject(br);

    // 绘制图标
    DrawIconEx(memDC, 0, 0, hIcon, size, size, 0, nullptr, DI_NORMAL);

    // 读取 RGBA 像素
    std::vector<uint8_t> pixels(size * size * 4);
    uint32_t* src = (uint32_t*)bits;
    uint8_t* dst = pixels.data();
    for (int i = 0; i < size * size; ++i) {
        uint32_t pixel = src[i];
        dst[0] = (pixel >> 16) & 0xFF;  // R
        dst[1] = (pixel >> 8) & 0xFF;   // G
        dst[2] = pixel & 0xFF;          // B
        dst[3] = (pixel >> 24) & 0xFF;  // A
        dst += 4;
    }

    SelectObject(memDC, oldBmp);
    DeleteObject(hBmp);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    DeleteDC(memDC);
    ReleaseDC(nullptr, hdc);

    // 注册为 RGBA 纹理
    TextureDesc desc;
    desc.width = size;
    desc.height = size;
    desc.format = TextureDesc::Format::RGBA8;
    desc.pixels = std::move(pixels);

    return m_resMgr->registerTexture(desc);
}

} // namespace geofinder
