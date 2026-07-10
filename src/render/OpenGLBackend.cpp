// 参考蓝图 §5.3 — OpenGLBackend 实现（Sprint 2）
//
// 基于 OpenGL 3.3 Core Profile 的最小渲染后端：
// - RectOp：清屏 + 纯色矩形（Sprint 1）
// - TextRunOp：字形图集采样 + 颜色乘算（Sprint 2）
// - 纹理注册/注销/局部更新
//
// TODO(Sprint3): 实现合批优化（batch merging）
// TODO(Sprint3): 实现 RoundedRectOp / IconOp 绘制
// TODO(Sprint12): 实现 ShadowOp / ApplyEffectOp / RenderTargetOp

#include "render/OpenGLBackend.hpp"
#include "render/CommandBuffer.hpp"
#include "resource/ResourceManager.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace geofinder {

// ── 内嵌 Shader（蓝图 §5.25；后续 Sprint 迁移到文件） ────────────

// -- Rect Vertex Shader -------------------------------------------------
static const char* kRectVertexShader = R"GLSL(
#version 330 core

layout(location = 0) in vec2 aPos;

uniform vec2 uScreenSize;

void main()
{
    vec2 ndc = (aPos / uScreenSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)GLSL";

// -- Rect Fragment Shader -----------------------------------------------
static const char* kRectFragmentShader = R"GLSL(
#version 330 core

out vec4 FragColor;

uniform vec4 uColor;

void main()
{
    FragColor = uColor;
}
)GLSL";

// -- Text Vertex Shader (Sprint 2) --------------------------------------
static const char* kTextVertexShader = R"GLSL(
#version 330 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

uniform vec2 uScreenSize;

out vec2 vUV;

void main()
{
    vec2 ndc = (aPos / uScreenSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
}
)GLSL";

// -- Text Fragment Shader (Sprint 2) ------------------------------------
static const char* kTextFragmentShader = R"GLSL(
#version 330 core

in vec2 vUV;

out vec4 FragColor;

uniform sampler2D uTexture;
uniform vec4 uColor;

void main()
{
    float alpha = texture(uTexture, vUV).r;
    FragColor = vec4(uColor.rgb, uColor.a * alpha);
}
)GLSL";

// -- Icon Fragment Shader (Sprint 9) — RGBA 纹理采样 ------------------
static const char* kIconFragmentShader = R"GLSL(
#version 330 core

in vec2 vUV;

out vec4 FragColor;

uniform sampler2D uTexture;
uniform vec4 uColor;

void main()
{
    vec4 texColor = texture(uTexture, vUV);
    FragColor = texColor * uColor;
}
)GLSL";

// ── 构造 / 析构 ───────────────────────────────────────────────────────

OpenGLBackend::OpenGLBackend() = default;

OpenGLBackend::~OpenGLBackend()
{
    shutdown();
}

// ── init ───────────────────────────────────────────────────────────────

bool OpenGLBackend::init(GLFWwindow* window)
{
    // 参考蓝图 §5.3 — 初始化 OpenGL 3.3 上下文 + GLEW
    m_window = window;
    if (!m_window) {
        std::fprintf(stderr, "[OpenGLBackend] null window\n");
        return false;
    }

    glfwMakeContextCurrent(m_window);

    // 初始化 GLEW（必须在 OpenGL context 激活后调用）
    glewExperimental = GL_TRUE;
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK) {
        std::fprintf(stderr, "[OpenGLBackend] glewInit failed: %s\n",
                     glewGetErrorString(glewErr));
        return false;
    }

    // 消费 glewInit 产生的 GL_INVALID_ENUM（已知副作用）
    glGetError();

    // ── 编译 Shader ──────────────────────────────────────────────

    if (!initRectShaders()) return false;
    if (!initTextShaders()) return false;
    if (!initIconShaders()) return false;

    // ── 创建 Rect VAO + VBO ──────────────────────────────────────

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // 顶点属性：位置 vec2（location=0）
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(float) * 2, (void*)0);

    glBindVertexArray(0);

    // ── 创建 Text VAO + VBO + EBO（Sprint 2） ───────────────────

    glGenVertexArrays(1, &m_textVao);
    glBindVertexArray(m_textVao);

    glGenBuffers(1, &m_textVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_textVbo);

    glGenBuffers(1, &m_textEbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_textEbo);

    // 顶点属性：
    //   location=0: aPos (vec2, offset=0)
    //   location=1: aUV  (vec2, offset=sizeof(float)*2)
    constexpr size_t kStride = sizeof(float) * 4;  // pos.xy + uv.xy
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          static_cast<GLsizei>(kStride), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          static_cast<GLsizei>(kStride),
                          (void*)(sizeof(float) * 2));

    glBindVertexArray(0);

    // ── 初始状态 ─────────────────────────────────────────────────

    glClearColor(0.15f, 0.15f, 0.15f, 1.0f);  // 深灰色背景
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    return true;
}

void OpenGLBackend::shutdown()
{
    // 参考蓝图 §5.3 — 清理所有 GPU 资源

    // Rect 资源
    if (m_rectProgram) {
        glDeleteProgram(m_rectProgram);
        m_rectProgram = 0;
    }
    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }

    // Text 资源（Sprint 2）
    if (m_textProgram) {
        glDeleteProgram(m_textProgram);
        m_textProgram = 0;
    }
    if (m_textVbo) {
        glDeleteBuffers(1, &m_textVbo);
        m_textVbo = 0;
    }
    if (m_textEbo) {
        glDeleteBuffers(1, &m_textEbo);
        m_textEbo = 0;
    }
    if (m_textVao) {
        glDeleteVertexArrays(1, &m_textVao);
        m_textVao = 0;
    }

    // Icon 资源（Sprint 9）
    if (m_iconProgram) { glDeleteProgram(m_iconProgram); m_iconProgram = 0; }

    // 清理遗留纹理
    for (auto& [handle, texId] : m_textures) {
        glDeleteTextures(1, &texId);
    }
    m_textures.clear();
    m_window = nullptr;
}

// ── Rect Shader 初始化 ────────────────────────────────────────────────

bool OpenGLBackend::initRectShaders()
{
    unsigned int vertShader = 0, fragShader = 0;
    if (!compileShader(GL_VERTEX_SHADER,   kRectVertexShader,   vertShader)) return false;
    if (!compileShader(GL_FRAGMENT_SHADER, kRectFragmentShader, fragShader)) return false;

    if (!linkProgram(vertShader, fragShader, m_rectProgram)) return false;

    glDeleteShader(vertShader);
    glDeleteShader(fragShader);

    m_rectUScreenSizeLoc = glGetUniformLocation(m_rectProgram, "uScreenSize");
    m_rectUColorLoc      = glGetUniformLocation(m_rectProgram, "uColor");

    return true;
}

// ── Text Shader 初始化（Sprint 2） ────────────────────────────────────

bool OpenGLBackend::initTextShaders()
{
    unsigned int vertShader = 0, fragShader = 0;
    if (!compileShader(GL_VERTEX_SHADER,   kTextVertexShader,   vertShader)) return false;
    if (!compileShader(GL_FRAGMENT_SHADER, kTextFragmentShader, fragShader)) return false;

    if (!linkProgram(vertShader, fragShader, m_textProgram)) return false;

    glDeleteShader(vertShader);
    glDeleteShader(fragShader);

    m_textUScreenSizeLoc = glGetUniformLocation(m_textProgram, "uScreenSize");
    m_textUColorLoc      = glGetUniformLocation(m_textProgram, "uColor");
    m_textUTextureLoc    = glGetUniformLocation(m_textProgram, "uTexture");

    return true;
}

// ── Icon Shader 初始化 ────────────────────────────────────────────────

bool OpenGLBackend::initIconShaders()
{
    unsigned int vertShader = 0, fragShader = 0;
    if (!compileShader(GL_VERTEX_SHADER,   kTextVertexShader,   vertShader)) return false;
    if (!compileShader(GL_FRAGMENT_SHADER, kIconFragmentShader, fragShader)) return false;

    if (!linkProgram(vertShader, fragShader, m_iconProgram)) return false;

    glDeleteShader(vertShader);
    glDeleteShader(fragShader);

    m_iconUScreenSizeLoc = glGetUniformLocation(m_iconProgram, "uScreenSize");
    m_iconUColorLoc      = glGetUniformLocation(m_iconProgram, "uColor");
    m_iconUTextureLoc    = glGetUniformLocation(m_iconProgram, "uTexture");

    return true;
}

// ── 帧循环 ─────────────────────────────────────────────────────────────

void OpenGLBackend::resize(int w, int h)
{
    m_width = w;
    m_height = h;
    glViewport(0, 0, w, h);
}

void OpenGLBackend::beginFrame()
{
    glClear(GL_COLOR_BUFFER_BIT);
}

void OpenGLBackend::endFrame()
{
    glFlush();
}

void OpenGLBackend::present()
{
    if (m_window) {
        glfwSwapBuffers(m_window);
    }
}

// ── 纹理资源 ───────────────────────────────────────────────────────────

void OpenGLBackend::onTextureRegistered(ResourceHandle handle,
                                        const TextureDesc& desc)
{
    // 参考蓝图 §5.3 — 创建 OpenGL 纹理对象
    unsigned int texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);

    // 默认参数
    // R8 图集（字形）用 NEAREST 避免相邻字形边缘流血；RGBA8 用 LINEAR 保持平滑。
    if (desc.format == TextureDesc::Format::R8) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLenum internalFormat = (desc.format == TextureDesc::Format::R8)
        ? GL_R8 : GL_RGBA8;
    GLenum dataFormat = (desc.format == TextureDesc::Format::R8)
        ? GL_RED : GL_RGBA;

    glTexImage2D(GL_TEXTURE_2D, 0,
                 static_cast<GLint>(internalFormat),
                 desc.width, desc.height, 0,
                 dataFormat, GL_UNSIGNED_BYTE,
                 desc.pixels.empty() ? nullptr : desc.pixels.data());

    glBindTexture(GL_TEXTURE_2D, 0);

    m_textures[handle] = texId;
}

void OpenGLBackend::onTextureUnregistered(ResourceHandle handle)
{
    // 参考蓝图 §5.3 — 立即删除纹理（OpenGL 驱动引用计数安全）
    auto it = m_textures.find(handle);
    if (it != m_textures.end()) {
        glDeleteTextures(1, &it->second);
        m_textures.erase(it);
    }
}

// ── 纹理局部更新（Sprint 2: FontManager 图集填充） ────────────────────

void OpenGLBackend::updateTextureRegion(ResourceHandle handle,
                                        int x, int y, int w, int h,
                                        const uint8_t* data)
{
    auto it = m_textures.find(handle);
    if (it == m_textures.end()) {
        std::fprintf(stderr, "[OpenGLBackend] updateTextureRegion: "
                     "unknown handle %llu\n",
                     static_cast<unsigned long long>(handle));
        return;
    }

    glBindTexture(GL_TEXTURE_2D, it->second);

    // FreeType 字形位图为紧致打包（1 字节/像素），
    // 必须设置 GL_UNPACK_ALIGNMENT=1，否则 OpenGL 默认 4 字节对齐
    // 会错误地读取填充字节，导致每行数据偏移扭曲。
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // 图集纹理为 GL_R8 格式，字形数据为单通道灰度
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    x, y, w, h,
                    GL_RED, GL_UNSIGNED_BYTE, data);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);  // 恢复默认
    glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLBackend::notifyResourceUsed(ResourceHandle /*handle*/)
{
    // OpenGL 不需要 Fence 追踪；空操作
}

// ── 命令执行 ───────────────────────────────────────────────────────────

void OpenGLBackend::execute(const CommandBuffer& cmds)
{
    // 参考蓝图 §5.3, §5.4 — 逐条执行命令
    // TODO(Sprint3): 实现合批优化（batch merging per §5.3）
    for (const auto& cmd : cmds.getCommands()) {
        // TODO(Sprint3): glScissor + stencil for ClipOp

        std::visit([this](const auto& op) {
            using T = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<T, RectOp>) {
                drawRectOp(op);
            } else if constexpr (std::is_same_v<T, TextRunOp>) {
                drawTextRunOp(op);
            } else if constexpr (std::is_same_v<T, IconOp>) {
                drawIconOp(op);
            }
            // 其他 PaintOp 变体：后续 Sprint 实现
            // TODO(Sprint3): RoundedRectOp
            // TODO(Sprint3): IconOp
            // TODO(Sprint12): ShadowOp, RenderTargetOp, ApplyEffectOp
        }, cmd.op);
    }
}

// ── 垃圾回收 ───────────────────────────────────────────────────────────

void OpenGLBackend::collectGarbage(uint64_t /*frameIndex*/)
{
    // OpenGL 驱动内部引用计数，无需延迟删除；空操作
}

// ── Shader 编译辅助 ────────────────────────────────────────────────────

bool OpenGLBackend::compileShader(unsigned int type,
                                  const char* source,
                                  unsigned int& outShader)
{
    outShader = glCreateShader(type);
    glShaderSource(outShader, 1, &source, nullptr);
    glCompileShader(outShader);

    GLint success = 0;
    glGetShaderiv(outShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(outShader, sizeof(infoLog), nullptr, infoLog);
        std::fprintf(stderr, "[OpenGLBackend] shader compile error (%s):\n%s\n",
                     (type == GL_VERTEX_SHADER) ? "vertex" : "fragment",
                     infoLog);
        glDeleteShader(outShader);
        outShader = 0;
        return false;
    }
    return true;
}

bool OpenGLBackend::linkProgram(unsigned int vertShader,
                                unsigned int fragShader,
                                unsigned int& outProgram)
{
    outProgram = glCreateProgram();
    glAttachShader(outProgram, vertShader);
    glAttachShader(outProgram, fragShader);
    glLinkProgram(outProgram);

    GLint success = 0;
    glGetProgramiv(outProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(outProgram, sizeof(infoLog), nullptr, infoLog);
        std::fprintf(stderr, "[OpenGLBackend] program link error:\n%s\n",
                     infoLog);
        glDeleteProgram(outProgram);
        outProgram = 0;
        return false;
    }
    return true;
}

// ── Rect 绘制 ──────────────────────────────────────────────────────────

void OpenGLBackend::drawRectOp(const RectOp& op)
{
    // 参考蓝图 §5.3 — 从 RectOp 生成 2 三角形 = 6 顶点
    float x1 = op.pos.x;
    float y1 = op.pos.y;
    float x2 = op.pos.x + op.size.x;
    float y2 = op.pos.y + op.size.y;

    // 两个三角形：TL-TR-BL, TR-BR-BL
    float vertices[] = {
        x1, y1,   // 左上
        x2, y1,   // 右上
        x1, y2,   // 左下
        x2, y1,   // 右上
        x2, y2,   // 右下
        x1, y2,   // 左下
    };

    glUseProgram(m_rectProgram);

    glUniform2f(m_rectUScreenSizeLoc,
                static_cast<float>(m_width),
                static_cast<float>(m_height));

    glUniform4f(m_rectUColorLoc, op.color.r, op.color.g,
                op.color.b, op.color.a);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,
                 GL_DYNAMIC_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
}

// ── Text 绘制（Sprint 2） ──────────────────────────────────────────────

void OpenGLBackend::drawTextRunOp(const TextRunOp& op)
{
    // 参考蓝图 §5.3 — 从 TextRunOp 渲染字形
    if (op.vertices.empty() || op.indices.empty()) return;

    // 查找图集纹理
    auto it = m_textures.find(op.textureHandle);
    if (it == m_textures.end()) return;

    glUseProgram(m_textProgram);

    // 上传 uniforms
    glUniform2f(m_textUScreenSizeLoc,
                static_cast<float>(m_width),
                static_cast<float>(m_height));

    float a = op.opacity;
    glUniform4f(m_textUColorLoc, op.color.r, op.color.g,
                op.color.b, op.color.a * a);

    // 绑定图集纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, it->second);
    glUniform1i(m_textUTextureLoc, 0);

    // 上传顶点 + 索引
    glBindVertexArray(m_textVao);

    glBindBuffer(GL_ARRAY_BUFFER, m_textVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(op.vertices.size() * sizeof(GlyphVertex)),
                 op.vertices.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_textEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(op.indices.size() * sizeof(uint32_t)),
                 op.indices.data(), GL_DYNAMIC_DRAW);

    glDrawElements(GL_TRIANGLES,
                   static_cast<GLsizei>(op.indices.size()),
                   GL_UNSIGNED_INT, nullptr);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ── Icon 绘制 ──────────────────────────────────────────────────────────

void OpenGLBackend::drawIconOp(const IconOp& op)
{
    auto it = m_textures.find(op.textureHandle);
    if (it == m_textures.end()) return;

    float x1 = op.pos.x, y1 = op.pos.y;
    float x2 = x1 + op.size.x, y2 = y1 + op.size.y;
    float verts[] = { x1,y1,0,0, x2,y1,1,0, x1,y2,0,1, x2,y2,1,1 };

    glUseProgram(m_iconProgram);
    glUniform2f(m_iconUScreenSizeLoc, (float)m_width, (float)m_height);
    glUniform4f(m_iconUColorLoc, op.tintColor.r, op.tintColor.g, op.tintColor.b, op.tintColor.a);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, it->second);
    glUniform1i(m_iconUTextureLoc, 0);
    glBindVertexArray(m_textVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_textVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace geofinder
