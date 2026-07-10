#pragma once

// 参考蓝图 §5.3 — OpenGLBackend 实现
//
// 基于 OpenGL 3.3 Core Profile 的渲染后端。持有 shader program、
// VAO、VBO 和纹理映射表。纹理资源的 GPU 创建/销毁在此完成。
//
// Sprint 2: 添加 TextRunOp 渲染支持（独立 text shader + VAO/VBO/EBO）。

#include "render/IRenderBackend.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace geofinder {

class OpenGLBackend : public IRenderBackend {
public:
    OpenGLBackend();
    ~OpenGLBackend() override;

    // ── IRenderBackend 接口实现 ────────────────────────────────
    bool init(GLFWwindow* window) override;
    void shutdown() override;

    void resize(int w, int h) override;
    void beginFrame() override;
    void endFrame() override;
    void present() override;

    void onTextureRegistered(ResourceHandle handle,
                             const TextureDesc& desc) override;
    void onTextureUnregistered(ResourceHandle handle) override;

    void updateTextureRegion(ResourceHandle handle,
                             int x, int y, int w, int h,
                             const uint8_t* data) override;

    void notifyResourceUsed(ResourceHandle handle) override;

    void execute(const CommandBuffer& cmds) override;

    void collectGarbage(uint64_t frameIndex) override;

private:
    // ── Shader 编译辅助 ──────────────────────────────────────
    bool compileShader(unsigned int type, const char* source,
                       unsigned int& outShader);
    bool linkProgram(unsigned int vertShader, unsigned int fragShader,
                     unsigned int& outProgram);

    // ── Shader 初始化 ────────────────────────────────────────
    bool initRectShaders();
    bool initTextShaders();
    bool initIconShaders();

    // ── 绘制方法 ──────────────────────────────────────────────
    void drawRectOp(const struct RectOp& op);
    void drawTextRunOp(const struct TextRunOp& op);
    void drawIconOp(const struct IconOp& op);

    // ── OpenGL 资源（Rect） ───────────────────────────────────
    GLFWwindow* m_window = nullptr;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_rectProgram = 0;
    unsigned int m_rectUScreenSizeLoc = 0;
    unsigned int m_rectUColorLoc = 0;

    // ── OpenGL 资源（Text — Sprint 2） ────────────────────────
    unsigned int m_textProgram = 0;
    unsigned int m_textVao = 0;
    unsigned int m_textVbo = 0;
    unsigned int m_textEbo = 0;
    unsigned int m_textUScreenSizeLoc = 0;
    unsigned int m_textUColorLoc = 0;
    unsigned int m_textUTextureLoc = 0;

    // ── OpenGL 资源（Icon — Sprint 9） ────────────────────────
    unsigned int m_iconProgram = 0;
    unsigned int m_iconUScreenSizeLoc = 0;
    unsigned int m_iconUColorLoc = 0;
    unsigned int m_iconUTextureLoc = 0;

    int m_width = 0;
    int m_height = 0;

    // 纹理句柄 → OpenGL 纹理 ID 映射
    std::unordered_map<ResourceHandle, unsigned int> m_textures;
};

} // namespace geofinder
