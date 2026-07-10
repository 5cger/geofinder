#pragma once

// 参考蓝图 §5.2 — ResourceManager 纹理资源管理
//
// ResourceManager 是 GPU 纹理资源的中央注册表。它持有像素数据的深拷贝
// （std::vector<uint8_t>），确保在渲染后端重建（resyncAllTextures）时
// 数据不会悬垂。线程安全由内部 std::mutex 保证。

#include "render/ResourceHandle.hpp"

#include <cstdint>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

namespace geofinder {

class IRenderBackend;

// ── 纹理描述符（蓝图 §5.2） ────────────────────────────────────

struct TextureDesc {
    enum class Format {
        R8,      // 单通道（字形图集）
        RGBA8    // 四通道（图标、颜色纹理）
    };

    int width = 0;
    int height = 0;
    Format format = Format::RGBA8;
    std::vector<uint8_t> pixels;  // 深拷贝，ResourceManager 持有所有权
};

// ── ResourceManager（蓝图 §5.2） ───────────────────────────────

class ResourceManager {
public:
    ResourceManager();
    ~ResourceManager();

    // 设置渲染后端（必须在使用纹理操作前调用）
    void setBackend(IRenderBackend* backend);

    // ── 持久纹理 ──────────────────────────────────────────────

    // 注册持久纹理（如字体图集）。返回非零句柄。
    ResourceHandle registerTexture(const TextureDesc& desc);

    // 注销持久纹理
    void unregisterTexture(ResourceHandle handle);

    // ── 临时纹理 ──────────────────────────────────────────────

    // 分配帧内临时纹理（如阴影/特效离屏渲染），帧末自动回收
    ResourceHandle allocateTemporary(int w, int h,
                                     TextureDesc::Format fmt = TextureDesc::Format::RGBA8);

    // 帧末调用：回收本帧分配的所有临时纹理
    void recycleTemporaries();

    // ── 后端重建 ──────────────────────────────────────────────

    // 后端重建后重新同步所有持久纹理
    void resyncAllTextures();

    // ── 调试 ──────────────────────────────────────────────────
    size_t getRegistrySize() const;

private:
    uint64_t m_nextHandle = 1;
    IRenderBackend* m_backend = nullptr;
    std::unordered_map<ResourceHandle, TextureDesc> m_registry;   // 持久纹理
    std::set<ResourceHandle> m_temporaryTextures;                  // 本帧临时纹理
    mutable std::mutex m_mutex;                                    // 线程安全
};

} // namespace geofinder
