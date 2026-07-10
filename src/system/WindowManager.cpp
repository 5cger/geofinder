// WindowManager — 自定义窗口（无边框 + 自绘标题栏）
#include "system/WindowManager.hpp"
#include "render/CommandBuffer.hpp"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <cstdio>
#include <dwmapi.h>

namespace geofinder {

WindowManager::WindowManager(int width, int height, const std::string& title)
    : m_width(width), m_height(height), m_title(title) {}
WindowManager::~WindowManager() { shutdown(); }

bool WindowManager::init()
{
    if (!glfwInit()) { std::fprintf(stderr, "[WM] glfwInit failed\n"); return false; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);  // 无边框

    m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), nullptr, nullptr);
    if (!m_window) { glfwTerminate(); return false; }
    glfwMakeContextCurrent(m_window);

    // DWM 窗口阴影
    HWND hwnd = glfwGetWin32Window(m_window);
    if (hwnd) {
        MARGINS m = {-1,-1,-1,-1}; DwmExtendFrameIntoClientArea(hwnd, &m);
        // 隐藏任务栏图标（WS_EX_TOOLWINDOW）
        LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
        SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_TOOLWINDOW);
    }

    glfwSetWindowUserPointer(m_window, this);
    glfwSwapInterval(1);
    registerCallbacks();
    return true;
}

void WindowManager::shutdown() {
    if (m_window) { glfwDestroyWindow(m_window); m_window = nullptr; glfwTerminate(); }
    m_visible = false;
}

GLFWwindow* WindowManager::getGLFWWindow() const { return m_window; }
bool WindowManager::shouldClose() const { return m_window && glfwWindowShouldClose(m_window); }
void WindowManager::show() { if (m_window) { glfwShowWindow(m_window); m_visible = true; } }
void WindowManager::hide() { if (m_window) { glfwHideWindow(m_window); m_visible = false; } }
bool WindowManager::isVisible() const { return m_visible; }

void WindowManager::setKeyCallback(KeyCallback cb) { m_keyCb = std::move(cb); }
void WindowManager::setCharCallback(CharCallback cb) { m_charCb = std::move(cb); }
void WindowManager::setMouseCallback(MouseCallback cb) { m_mouseCb = std::move(cb); }
void WindowManager::setCursorCallback(CursorCallback cb) { m_cursorCb = std::move(cb); }

void WindowManager::registerCallbacks()
{
    if (!m_window) return;
    glfwSetKeyCallback(m_window, [](GLFWwindow* w, int k, int s, int a, int m) {
        auto* wm = static_cast<WindowManager*>(glfwGetWindowUserPointer(w));
        if (wm && wm->m_keyCb) wm->m_keyCb(k, s, a, m);
    });
    glfwSetCharCallback(m_window, [](GLFWwindow* w, unsigned int c) {
        auto* wm = static_cast<WindowManager*>(glfwGetWindowUserPointer(w));
        if (wm && wm->m_charCb) wm->m_charCb(c);
    });
    glfwSetMouseButtonCallback(m_window, [](GLFWwindow* w, int b, int a, int m) {
        auto* wm = static_cast<WindowManager*>(glfwGetWindowUserPointer(w));
        if (wm && wm->m_mouseCb) wm->m_mouseCb(b, a, m);
    });
    glfwSetCursorPosCallback(m_window, [](GLFWwindow* w, double x, double y) {
        auto* wm = static_cast<WindowManager*>(glfwGetWindowUserPointer(w));
        if (wm && wm->m_cursorCb) wm->m_cursorCb(x, y);
    });
}

// ── 标题栏 ──────────────────────────────────────────────────────

void WindowManager::paintTitleBar(CommandBuffer& cmdBuf) const
{
    if (!m_window) return;
    int w, h; glfwGetWindowSize(m_window, &w, &h);

    // 背景
    { RenderCommand c; RectOp r;
      r.pos = glm::vec2(0,0); r.size = glm::vec2((float)w, (float)m_titleBarHeight);
      r.color = glm::vec4(0.06f, 0.06f, 0.09f, 0.98f); c.op = r; cmdBuf.add(c); }
    // 分隔线
    { RenderCommand c; RectOp r;
      r.pos = glm::vec2(0, (float)m_titleBarHeight-1); r.size = glm::vec2((float)w, 1);
      r.color = glm::vec4(0.15f, 0.15f, 0.2f, 1); c.op = r; cmdBuf.add(c); }
    // 关闭按钮
    { RenderCommand c; RectOp r;
      float bx = (float)w - 36;
      r.pos = glm::vec2(bx, 4); r.size = glm::vec2(28, 20);
      r.color = glm::vec4(0.85f, 0.25f, 0.25f, 0.7f); c.op = r; cmdBuf.add(c); }
}

bool WindowManager::onTitleBarMouse(int button, int action, double x, double y)
{
    if (!m_window) return false;
    if (y >= (double)m_titleBarHeight) return false;

    if (button == 0 && action == GLFW_PRESS) {
        int w, h; glfwGetWindowSize(m_window, &w, &h);
        if (x > (double)w - 36.0 && y < 24.0) {
            if (m_closeCb) m_closeCb(); return true;
        }
        m_dragging = true; m_dragStartX = x; m_dragStartY = y; return true;
    }
    if (button == 0 && action == GLFW_RELEASE) m_dragging = false;
    return m_dragging;
}

void WindowManager::onTitleBarCursor(double x, double y)
{
    if (!m_dragging || !m_window) return;
    int wx, wy; glfwGetWindowPos(m_window, &wx, &wy);
    glfwSetWindowPos(m_window, wx + (int)(x - m_dragStartX), wy + (int)(y - m_dragStartY));
}

} // namespace geofinder
