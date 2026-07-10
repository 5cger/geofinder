// 参考蓝图 §5.11 — WindowManager（Sprint 3：回调路由激活）
//
// Sprint 1: 最小实现 — 窗口创建/销毁/显隐
// Sprint 3: 添加键盘/字符/鼠标/光标回调路由
// Sprint 10: IME 子类化
#pragma once

#include <functional>
#include <memory>
#include <string>

struct GLFWwindow;

namespace geofinder {

class CommandBuffer;  // forward

// ── 回调类型别名 ──────────────────────────────────────────────────

using KeyCallback    = std::function<void(int key, int scancode, int action, int mods)>;
using CharCallback   = std::function<void(unsigned int codepoint)>;
using MouseCallback  = std::function<void(int button, int action, int mods)>;
using CursorCallback = std::function<void(double x, double y)>;

// ── WindowManager ──────────────────────────────────────────────

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
    int getTitleBarHeight() const { return m_titleBarHeight; }

    // 标题栏渲染（自定义窗口）
    void paintTitleBar(CommandBuffer& cmdBuf) const;
    bool onTitleBarMouse(int button, int action, double x, double y);
    void onTitleBarCursor(double x, double y);

    // ── 回调设置（Sprint 3 激活） ────────────────────────────────
    void setKeyCallback(KeyCallback cb);
    void setCharCallback(CharCallback cb);
    void setMouseCallback(MouseCallback cb);
    void setCursorCallback(CursorCallback cb);

private:
    void registerCallbacks();

    int m_width;
    int m_height;
    std::string m_title;
    GLFWwindow* m_window = nullptr;
    bool m_visible = false;

    // ── 回调（Sprint 3 激活） ────────────────────────────────────
    KeyCallback    m_keyCb;
    CharCallback   m_charCb;
    MouseCallback  m_mouseCb;
    CursorCallback m_cursorCb;

    // 标题栏拖拽
    static constexpr int m_titleBarHeight = 28;
    mutable bool m_dragging = false;
    mutable double m_dragStartX = 0;
    mutable double m_dragStartY = 0;

    // 关闭回调
    using CloseCallback = std::function<void()>;
    CloseCallback m_closeCb;
public:
    void setCloseCallback(CloseCallback cb) { m_closeCb = std::move(cb); }
};

} // namespace geofinder
