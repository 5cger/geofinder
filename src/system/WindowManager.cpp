// WindowManager — 无边框窗口 + 全窗口拖拽 + DWM 阴影
#include "system/WindowManager.hpp"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <cstdio>
#include <dwmapi.h>

namespace geofinder {

WindowManager::WindowManager(int w, int h, const std::string& t)
    : m_width(w), m_height(h), m_title(t) {}
WindowManager::~WindowManager() { shutdown(); }

bool WindowManager::init() {
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), nullptr, nullptr);
    if (!m_window) { glfwTerminate(); return false; }
    glfwMakeContextCurrent(m_window);
    HWND h = glfwGetWin32Window(m_window);
    if (h) {
        MARGINS m = {-1,-1,-1,-1}; DwmExtendFrameIntoClientArea(h, &m);
        LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
        SetWindowLongW(h, GWL_EXSTYLE, ex | WS_EX_TOOLWINDOW);
    }
    glfwSetWindowUserPointer(m_window, this);
    glfwSwapInterval(1);
    if (m_window) {
        glfwSetKeyCallback(m_window, [](GLFWwindow* w, int k, int s, int a, int m) {
            auto* p = (WindowManager*)glfwGetWindowUserPointer(w); if (p && p->m_keyCb) p->m_keyCb(k,s,a,m); });
        glfwSetCharCallback(m_window, [](GLFWwindow* w, unsigned int c) {
            auto* p = (WindowManager*)glfwGetWindowUserPointer(w); if (p && p->m_charCb) p->m_charCb(c); });
        glfwSetMouseButtonCallback(m_window, [](GLFWwindow* w, int b, int a, int m) {
            auto* p = (WindowManager*)glfwGetWindowUserPointer(w); if (p && p->m_mouseCb) p->m_mouseCb(b,a,m); });
        glfwSetCursorPosCallback(m_window, [](GLFWwindow* w, double x, double y) {
            auto* p = (WindowManager*)glfwGetWindowUserPointer(w); if (p && p->m_cursorCb) p->m_cursorCb(x,y); });
    }
    return true;
}

void WindowManager::shutdown() {
    if (m_window) { glfwDestroyWindow(m_window); m_window = nullptr; glfwTerminate(); } m_visible = false;
}
GLFWwindow* WindowManager::getGLFWWindow() const { return m_window; }
bool WindowManager::shouldClose() const { return m_window && glfwWindowShouldClose(m_window); }
void WindowManager::show() {
    if (m_window) {
        glfwShowWindow(m_window);
        // 防止抢走焦点
        HWND h = glfwGetWin32Window(m_window);
        if (h) ShowWindow(h, SW_SHOWNOACTIVATE);
        m_visible = true;
    }
}
void WindowManager::hide() { if (m_window) { glfwHideWindow(m_window); m_visible=false; } }
bool WindowManager::isVisible() const { return m_visible; }

void WindowManager::resize(int w, int h) {
    if (m_window) {
        m_width = w; m_height = h;
        glfwSetWindowSize(m_window, w, h);
    }
}

bool WindowManager::onWindowDrag(int btn, int act, double x, double y) {
    if (!m_window || btn != 0) return false;
    if (act == GLFW_PRESS) { m_dragging=true; m_dragStartX=x; m_dragStartY=y; return true; }
    if (act == GLFW_RELEASE) { m_dragging=false; return false; }
    return false;
}
void WindowManager::onWindowDragMove(double x, double y) {
    if (!m_dragging || !m_window) return;
    int wx,wy; glfwGetWindowPos(m_window,&wx,&wy);
    glfwSetWindowPos(m_window,wx+(int)(x-m_dragStartX),wy+(int)(y-m_dragStartY));
}

} // namespace geofinder
