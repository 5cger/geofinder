#pragma once

// 参考蓝图 §5.4 — CommandBuffer 及相关绘制操作类型
//
// CommandBuffer 是 UI 层与渲染后端的接口：UI 控件产生 RenderCommand，
// 渲染后端遍历并执行。Sprint 1 仅实现 RectOp 的绘制；
// 其他 PaintOp 变体在后续 Sprint 逐步激活。

#include "render/ResourceHandle.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <variant>
#include <vector>

namespace geofinder {

// ── 混合模式（蓝图 §5.4） ──────────────────────────────────────

enum class BlendMode {
    Normal,
    Multiply,
    Screen
};

// ── 裁剪操作（蓝图 §5.4） ──────────────────────────────────────

struct ClipRect {
    int x, y, w, h;
};

struct ClipRoundedRect {
    int x, y, w, h;
    float radius;
};

// std::monostate = 无裁剪（不继承上一次裁剪状态）
using ClipOp = std::variant<std::monostate, ClipRect, ClipRoundedRect>;

// ── 字形顶点（蓝图 §5.4） ──────────────────────────────────────

struct GlyphVertex {
    glm::vec2 pos;   // 屏幕坐标
    glm::vec2 uv;    // 图集 UV
};

// ── 绘制操作变体（蓝图 §5.4） ──────────────────────────────────

struct RectOp {
    glm::vec2 pos;
    glm::vec2 size;
    glm::vec4 color;
};

struct RoundedRectOp {
    glm::vec2 pos;
    glm::vec2 size;
    float radius;
    glm::vec4 color;
};

struct TextRunOp {
    ResourceHandle textureHandle;
    std::vector<GlyphVertex> vertices;
    std::vector<uint32_t> indices;
    glm::vec4 color;
    float opacity = 1.0f;
};

struct IconOp {
    ResourceHandle textureHandle;
    glm::vec2 pos;
    glm::vec2 size;
    glm::vec4 tintColor = glm::vec4(1.0f);
};

struct ShadowOp {
    ResourceHandle sourceTexture;
    float blurRadius;
    glm::vec2 offset;
    glm::vec4 color;
};

struct RenderTargetOp {
    enum class Action { Set, Reset, Blit };
    Action action;
    ResourceHandle targetTexture;
    ResourceHandle srcTexture;
    glm::vec4 clearColor;
};

struct ApplyEffectOp {
    enum class Type { GaussianBlur, Glow, ColorShift };
    Type type;
    float intensity;
    ResourceHandle inputTexture;
    ResourceHandle outputTexture;
};

using PaintOp = std::variant<
    RectOp,
    RoundedRectOp,
    TextRunOp,
    IconOp,
    ShadowOp,
    RenderTargetOp,
    ApplyEffectOp
>;

// ── 渲染命令（蓝图 §5.4） ──────────────────────────────────────

struct RenderCommand {
    PaintOp op;
    BlendMode blend = BlendMode::Normal;
    ClipOp clip;   // std::monostate = 无裁剪
};

// ── 命令缓冲区（蓝图 §5.4） ────────────────────────────────────

class CommandBuffer {
public:
    void clear();
    void add(const RenderCommand& cmd);

    const std::vector<RenderCommand>& getCommands() const;

private:
    std::vector<RenderCommand> m_commands;
};

} // namespace geofinder
