#pragma once

// 参考蓝图 §5.3 — IRenderBackend 抽象接口
//
// 所有 GPU 渲染操作通过此接口抽象，使得上层代码（UI、Resource）
// 不依赖具体图形 API。Sprint 1 仅实现 OpenGLBackend；
// VulkanBackend 在 Phase 3 实现。

#include "render/ResourceHandle.hpp"

#include <cstdint>

// 前向声明
struct GLFWwindow;

namespace geofinder {

class CommandBuffer;
struct TextureDesc;

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    // ── 生命周期 ──────────────────────────────────────────────
    virtual bool init(GLFWwindow* window) = 0;
    virtual void shutdown() = 0;

    // ── 帧循环 ────────────────────────────────────────────────
    virtual void resize(int w, int h) = 0;
    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;
    virtual void present() = 0;

    // ── 资源回调（由 ResourceManager 调用） ────────────────────
    virtual void onTextureRegistered(ResourceHandle handle,
                                     const TextureDesc& desc) = 0;
    virtual void onTextureUnregistered(ResourceHandle handle) = 0;

    // ── 纹理局部更新（Sprint 2: FontManager 图集填充） ─────────
    virtual void updateTextureRegion(ResourceHandle handle,
                                     int x, int y, int w, int h,
                                     const uint8_t* data) = 0;

    // ── 帧内资源追踪（Vulkan Fence 需要；OpenGL 空操作） ───────
    virtual void notifyResourceUsed(ResourceHandle handle) = 0;

    // ── 命令执行 ──────────────────────────────────────────────
    virtual void execute(const CommandBuffer& cmds) = 0;

    // ── 延迟回收（Vulkan 需要 Fence 等待；OpenGL 空操作） ──────
    virtual void collectGarbage(uint64_t frameIndex) = 0;
};

} // namespace geofinder
