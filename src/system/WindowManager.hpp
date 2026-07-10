// 参考蓝图 §5.11 — WindowManager
// 无边框窗口 + 全窗口拖拽 + DWM 阴影
#pragma once

#include <functional>
#include <memory>
#include <string>

struct GLFWwindow;

namespace geofinder {

using KeyCallback    = std::function<void(int key, int scancode, int action, int mods)>;
using CharCallback   = std::function<void(unsigned int codepoint)>;
using MouseCallback  = std::function<void(int button, int action, int mods)>;
using CursorCallback = std::function<void(double x, double y)>;
using CloseCallback  = std::function<void()>;

class WindowManager {
public:
    WindowManager(int width, int height, const std::string& title);
    ~WindowManager();

    bool init();
    void shutdown();

    GLFWwindow* getGLFWWindow() const;

    bool shouldClose() const;
    void show();
    void hide();
    bool isVisible() const;

    // 窗口拖拽（全窗口可拖拽）
    bool onWindowDrag(int button, int action, double x, double y);
    void onWindowDragMove(double x, double y);

    void setCloseCallback(CloseCallback cb) { m_closeCb = std::move(cb); }
    void setKeyCallback(KeyCallback cb) { m_keyCb = std::move(cb); }
    void setCharCallback(CharCallback cb) { m_charCb = std::move(cb); }
    void setMouseCallback(MouseCallback cb) { m_mouseCb = std::move(cb); }
    void setCursorCallback(CursorCallback cb) { m_cursorCb = std::move(cb); }

private:
    void registerCallbacks();

    int m_width, m_height;
    std::string m_title;
    GLFWwindow* m_window = nullptr;
    bool m_visible = false;

    CloseCallback   m_closeCb;
    KeyCallback     m_keyCb;
    CharCallback    m_charCb;
    MouseCallback   m_mouseCb;
    CursorCallback  m_cursorCb;

    mutable bool m_dragging = false;
    mutable double m_dragStartX = 0, m_dragStartY = 0;
};

} // namespace geofinder
