// InputField — 输入框控件（完全动画版：平滑光标闪烁 + 水平滑动）
#include "ui/InputField.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cwchar>

namespace geofinder {

static constexpr float kCursorSpeed = 14.0f;

InputField::InputField(FontManager* fontMgr)
    : m_fontMgr(fontMgr)
    , m_placeholder(L"输入搜索关键词...")
{
    m_size = glm::vec2(600.0f, 56.0f);
}

// ── 计算光标 X 位置 ────────────────────────────────────────────

static float calcCursorX(FontManager* fm, const std::wstring& text, int pos,
                         float fontSize, float paddingX)
{
    (void)paddingX;
    if (!fm || pos <= 0) return 0.0f;
    std::wstring prefix = text.substr(0, pos);
    auto run = fm->createTextRun(prefix, fontSize, 9999.0f, glm::vec4(1.0f));
    return run.totalWidth;
}

// ── 每帧更新 ────────────────────────────────────────────────────

bool InputField::update(float delta)
{
    if (!m_focused) {
        m_cursorAlpha = 0.0f;
        m_cursorTimer = 0.0f;
        return false;
    }

    float dt = std::min(delta, 0.1f);

    // 平滑光标呼吸：sin 波形
    m_cursorTimer += dt * 3.0f;
    float alphaTarget = (std::sin(m_cursorTimer * 3.14159f) + 1.0f) * 0.5f;

    float oldAlpha = m_cursorAlpha;
    m_cursorAlpha += (alphaTarget - m_cursorAlpha) * 12.0f * dt;
    m_cursorAlpha = std::max(0.0f, std::min(1.0f, m_cursorAlpha));

    // 光标水平滑动动画
    m_cursorVisualX += (m_cursorTargetX - m_cursorVisualX) * kCursorSpeed * dt;

    return std::abs(m_cursorAlpha - oldAlpha) > 0.001f ||
           std::abs(m_cursorTargetX - m_cursorVisualX) > 0.5f;
}

// ── 光标位置变更时同步目标 X ─────────────────────────────────

void InputField::syncCursorTargetX()
{
    m_cursorTargetX = calcCursorX(m_fontMgr, m_text, m_cursorPos,
                                   m_fontSize, m_paddingX);
}

// ── 内容操作 ──────────────────────────────────────────────────────

void InputField::setText(const std::wstring& text)
{
    m_text = text;
    m_cursorPos = static_cast<int>(m_text.size());
    syncCursorTargetX();
    m_cursorVisualX = m_cursorTargetX;
}

void InputField::clear()
{
    m_text.clear();
    m_cursorPos = 0;
    m_cursorTargetX = 0.0f;
    m_cursorVisualX = 0.0f;
}

// ── 键盘事件 ──────────────────────────────────────────────────────

bool InputField::onKeyEvent(const KeyEvent& evt)
{
    if (evt.action != GLFW_PRESS && evt.action != GLFW_REPEAT)
        return false;

    switch (evt.key) {
    case GLFW_KEY_BACKSPACE:
        if (m_cursorPos > 0 && !m_text.empty()) {
            m_text.erase(m_cursorPos - 1, 1);
            m_cursorPos--;
            syncCursorTargetX();
            if (m_onChange) m_onChange(m_text);
        }
        return true;
    case GLFW_KEY_DELETE:
        if (m_cursorPos < static_cast<int>(m_text.size()) && !m_text.empty()) {
            m_text.erase(m_cursorPos, 1);
            syncCursorTargetX();
            if (m_onChange) m_onChange(m_text);
        }
        return true;
    case GLFW_KEY_LEFT:
        if (m_cursorPos > 0) { m_cursorPos--; syncCursorTargetX(); }
        return true;
    case GLFW_KEY_RIGHT:
        if (m_cursorPos < static_cast<int>(m_text.size()))
            { m_cursorPos++; syncCursorTargetX(); }
        return true;
    case GLFW_KEY_HOME:
        m_cursorPos = 0; syncCursorTargetX();
        return true;
    case GLFW_KEY_END:
        m_cursorPos = static_cast<int>(m_text.size()); syncCursorTargetX();
        return true;
    default:
        return false;
    }
}

// ── 字符事件 ──────────────────────────────────────────────────────

bool InputField::onCharEvent(const CharEvent& evt)
{
    if (evt.codepoint < 0x20) return false;
    if (evt.codepoint == 0x7F) return false;

    m_text.insert(m_cursorPos, 1, static_cast<wchar_t>(evt.codepoint));
    m_cursorPos++;
    syncCursorTargetX();
    if (m_onChange) m_onChange(m_text);
    return true;
}

// ── 绘制 ──────────────────────────────────────────────────────────

void InputField::paint(CommandBuffer& cmdBuf)
{
    if (!m_visible) return;

    // 背景
    {
        RenderCommand bgCmd;
        RectOp bg;
        bg.pos = m_pos;
        bg.size = m_size;
        bg.color = glm::vec4(0.12f, 0.12f, 0.12f, 0.95f);
        bgCmd.op = bg;
        cmdBuf.add(bgCmd);
    }

    // 底部高亮线（焦点指示）
    {
        RenderCommand lineCmd;
        RectOp line;
        line.pos = glm::vec2(m_pos.x, m_pos.y + m_size.y - 2.0f);
        line.size = glm::vec2(m_size.x, 2.0f);
        line.color = m_focused
            ? glm::vec4(0.3f, 0.6f, 1.0f, 1.0f)
            : glm::vec4(0.3f, 0.3f, 0.3f, 1.0f);
        lineCmd.op = line;
        cmdBuf.add(lineCmd);
    }

    std::wstring displayText = m_text.empty() ? m_placeholder : m_text;
    bool isPlaceholder = m_text.empty();
    glm::vec4 textColor = isPlaceholder
        ? glm::vec4(0.5f, 0.5f, 0.5f, 1.0f)
        : glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

    if (!displayText.empty() && m_fontMgr) {
        float textMaxW = m_size.x - m_paddingX * 2.0f;
        auto textRun = m_fontMgr->createTextRun(displayText, m_fontSize,
                                                  textMaxW, textColor);
        if (textRun.atlasHandle != kInvalidHandle && !textRun.vertices.empty()) {
            float offsetX = m_pos.x + m_paddingX;
            float offsetY = m_pos.y + (m_size.y - m_fontSize) * 0.5f + m_fontSize;

            RenderCommand textCmd;
            TextRunOp textOp;
            textOp.textureHandle = textRun.atlasHandle;
            textOp.vertices.resize(textRun.vertices.size());
            for (size_t i = 0; i < textRun.vertices.size(); ++i) {
                textOp.vertices[i].pos = glm::vec2(
                    textRun.vertices[i].pos.x + offsetX,
                    textRun.vertices[i].pos.y + offsetY - m_fontSize);
                textOp.vertices[i].uv = textRun.vertices[i].uv;
            }
            textOp.indices = std::move(textRun.indices);
            textOp.color = textColor;
            textOp.opacity = 1.0f;
            textCmd.op = textOp;
            cmdBuf.add(textCmd);

            // 光标（平滑 alpha + 滑动 X）
            if (m_focused && m_cursorAlpha > 0.05f) {
                float cursorX = offsetX + m_cursorVisualX;
                RenderCommand cursorCmd;
                RectOp cursor;
                cursor.pos = glm::vec2(cursorX, m_pos.y + m_paddingY);
                cursor.size = glm::vec2(2.0f, m_size.y - m_paddingY * 2.0f);
                cursor.color = glm::vec4(0.3f, 0.6f, 1.0f, m_cursorAlpha);
                cursorCmd.op = cursor;
                cmdBuf.add(cursorCmd);
            }
        }
    } else {
        // 空输入框光标
        if (m_focused && m_cursorAlpha > 0.05f) {
            float cursorX = m_pos.x + m_paddingX;
            RenderCommand cursorCmd;
            RectOp cursor;
            cursor.pos = glm::vec2(cursorX, m_pos.y + m_paddingY);
            cursor.size = glm::vec2(2.0f, m_size.y - m_paddingY * 2.0f);
            cursor.color = glm::vec4(0.3f, 0.6f, 1.0f, m_cursorAlpha);
            cursorCmd.op = cursor;
            cmdBuf.add(cursorCmd);
        }
    }
}

} // namespace geofinder
