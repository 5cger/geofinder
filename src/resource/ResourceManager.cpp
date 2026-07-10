// 参考蓝图 §5.2 — ResourceManager 实现
//
// 纹理资源的中央注册表。持有像素数据的深拷贝，
// 线程安全由 std::mutex 保证。

#include "resource/ResourceManager.hpp"
#include "render/IRenderBackend.hpp"

#include <cstdint>

namespace geofinder {

// ── 构造 / 析构 ────────────────────────────────────────────────

ResourceManager::ResourceManager() = default;

ResourceManager::~ResourceManager()
{
    // 析构时不自动清理纹理 — 调用者应在 shutdown() 中显式注销。
    // 如果后端仍然存活，注销所有残留纹理。
    if (m_backend) {
        for (auto& [handle, desc] : m_registry) {
            m_backend->onTextureUnregistered(handle);
        }
    }
    m_registry.clear();
    m_temporaryTextures.clear();
}

// ── 后端绑定 ───────────────────────────────────────────────────

void ResourceManager::setBackend(IRenderBackend* backend)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_backend = backend;
}

// ── 持久纹理 ───────────────────────────────────────────────────

ResourceHandle ResourceManager::registerTexture(const TextureDesc& desc)
{
    // 参考蓝图 §5.2 — 注册纹理，深拷贝像素数据
    std::lock_guard<std::mutex> lock(m_mutex);

    ResourceHandle handle = m_nextHandle++;

    // 深拷贝像素数据（红色警戒线 #3：严禁存储外部指针）
    TextureDesc copy = desc;
    copy.pixels = desc.pixels;  // std::vector 自动深拷贝

    m_registry[handle] = std::move(copy);

    if (m_backend) {
        m_backend->onTextureRegistered(handle, m_registry[handle]);
    }

    return handle;
}

void ResourceManager::unregisterTexture(ResourceHandle handle)
{
    // 参考蓝图 §5.2 — 注销纹理
    if (handle == kInvalidHandle) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_registry.find(handle);
    if (it == m_registry.end()) return;

    if (m_backend) {
        m_backend->onTextureUnregistered(handle);
    }

    m_registry.erase(it);
    m_temporaryTextures.erase(handle);
}

// ── 临时纹理 ───────────────────────────────────────────────────

ResourceHandle ResourceManager::allocateTemporary(int w, int h,
                                                   TextureDesc::Format fmt)
{
    // 参考蓝图 §5.2 — 分配帧内临时纹理
    TextureDesc desc;
    desc.width = w;
    desc.height = h;
    desc.format = fmt;
    // 临时纹理不需要像素数据（由渲染后端填充）

    ResourceHandle handle = registerTexture(desc);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_temporaryTextures.insert(handle);
    }

    return handle;
}

void ResourceManager::recycleTemporaries()
{
    // 参考蓝图 §5.2 — 帧末回收所有临时纹理
    std::lock_guard<std::mutex> lock(m_mutex);

    for (ResourceHandle handle : m_temporaryTextures) {
        if (m_backend) {
            m_backend->onTextureUnregistered(handle);
        }
        m_registry.erase(handle);
    }
    m_temporaryTextures.clear();
}

// ── 后端重建 ───────────────────────────────────────────────────

void ResourceManager::resyncAllTextures()
{
    // 参考蓝图 §5.2 — 后端重建后重新同步所有纹理
    // 像素数据已深拷贝在 m_registry 中，不会悬垂
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_backend) return;

    for (auto& [handle, desc] : m_registry) {
        m_backend->onTextureRegistered(handle, desc);
    }
}

// ── 调试 ───────────────────────────────────────────────────────

size_t ResourceManager::getRegistrySize() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_registry.size();
}

} // namespace geofinder
