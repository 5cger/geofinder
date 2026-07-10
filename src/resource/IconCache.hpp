// IconCache — 系统文件图标缓存
//
// 按扩展名缓存图标：调用 SHGetFileInfoW 获取系统图标，
// 转换为 RGBA 纹理存入 ResourceManager，每扩展名只提取一次。
#pragma once

#include "render/ResourceHandle.hpp"
#include <string>
#include <unordered_map>
#include <windows.h>

namespace geofinder {

class ResourceManager;

class IconCache {
public:
    explicit IconCache(ResourceManager* resMgr);
    ~IconCache();

    /// 获取/加载扩展名对应的图标纹理句柄
    /// @param ext 扩展名（如 ".pdf", ".txt"）
    /// @return 纹理句柄，失败返回 kInvalidHandle
    ResourceHandle getIcon(const std::wstring& ext, int size = 24);

    /// 获取/加载特定文件的图标（带缓存）
    ResourceHandle getFileIcon(const std::wstring& path, int size = 24);

private:
    ResourceManager* m_resMgr;

    struct IconKey {
        std::wstring ext;
        int size;
        bool operator==(const IconKey& o) const { return ext == o.ext && size == o.size; }
    };
    struct IconKeyHash {
        size_t operator()(const IconKey& k) const {
            return std::hash<std::wstring>{}(k.ext) ^ (size_t)k.size;
        }
    };

    std::unordered_map<IconKey, ResourceHandle, IconKeyHash> m_cache;

    // 按实际文件路径缓存（.exe 内嵌图标、.lnk 目标图标等）
    std::unordered_map<std::wstring, ResourceHandle> m_pathCache;

    /// 从 HICON 提取 RGBA 像素并注册纹理
    ResourceHandle extractIcon(HICON hIcon, int size);

    /// 从实际文件提取图标（内部使用 SHGetFileInfoW 直接读取）
    ResourceHandle loadIconFromFile(const std::wstring& path, int size);
};

} // namespace geofinder
