// 参考蓝图 §5.7 — Widget 基类 + 事件类型
#pragma once

#include "render/CommandBuffer.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <string>

namespace geofinder {

struct MouseEvent {
    enum Type { Press, Release, Move };
    Type type;
    glm::vec2 pos;
    int button;  // 0=left, 1=right, 2=middle
};

struct KeyEvent {
    int key;       // GLFW key code
    int scancode;
    int action;    // GLFW_PRESS, GLFW_RELEASE, GLFW_REPEAT
    int mods;      // GLFW mods
};

struct CharEvent {
    uint32_t codepoint;
};

class Widget {
public:
    virtual ~Widget() = default;

    virtual void layout(const glm::vec2& pos, const glm::vec2& size) {
        m_pos = pos;
        m_size = size;
    }

    virtual void paint(CommandBuffer& cmdBuf) = 0;

    // 事件处理（默认返回 false = 未处理）
    virtual bool onMouseEvent(const MouseEvent& evt) { (void)evt; return false; }
    virtual bool onKeyEvent(const KeyEvent& evt) { (void)evt; return false; }
    virtual bool onCharEvent(const CharEvent& evt) { (void)evt; return false; }

    // 命中测试：返回此坐标是否在本 Widget 范围内
    virtual bool hitTest(const glm::vec2& pos) const {
        return pos.x >= m_pos.x && pos.x <= m_pos.x + m_size.x &&
               pos.y >= m_pos.y && pos.y <= m_pos.y + m_size.y;
    }

    const glm::vec2& getPos() const { return m_pos; }
    const glm::vec2& getSize() const { return m_size; }
    void setVisible(bool v) { m_visible = v; }
    bool isVisible() const { return m_visible; }

protected:
    glm::vec2 m_pos{0, 0};
    glm::vec2 m_size{0, 0};
    bool m_visible = true;
};

} // namespace geofinder
