// 参考蓝图 §5.13 — TrayManager 实现
//
// 创建隐藏消息窗口（HWND_MESSAGE）专门接收托盘通知，
// 避免托盘消息被 GLFW 的内部消息循环吞没。

#include "system/TrayManager.hpp"

#include <cstdio>
#include <shellapi.h>

namespace geofinder {

const wchar_t* TrayManager::kWindowClass = L"GeoFinderTrayWnd";

TrayManager::TrayManager()
{
    m_taskbarRestartMsg = RegisterWindowMessageW(L"TaskbarCreated");
}

TrayManager::~TrayManager()
{
    shutdown();
}

bool TrayManager::init()
{
    if (m_created) return true;

    // ── 注册隐藏窗口类 ──────────────────────────────────────
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;

    // 如果已注册则跳过（多次 init 场景）
    ATOM atom = GetClassInfoExW(wc.hInstance, kWindowClass, &wc);
    if (!atom) {
        atom = RegisterClassExW(&wc);
        if (!atom) {
            std::fprintf(stderr, "[TrayManager] RegisterClassEx failed (err=%lu)\n",
                         GetLastError());
            return false;
        }
    }

    // ── 创建隐藏消息窗口 ────────────────────────────────────
    m_hwnd = CreateWindowExW(0, kWindowClass, L"",
                              WS_POPUP,
                              0, 0, 0, 0,
                              HWND_MESSAGE,  // 仅消息，不可见
                              nullptr, wc.hInstance, this);
    if (!m_hwnd) {
        std::fprintf(stderr, "[TrayManager] CreateWindowEx failed (err=%lu)\n",
                     GetLastError());
        return false;
    }

    // ── 创建托盘图标 ────────────────────────────────────────
    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(m_nid.szTip, L"GeoFinder");

    if (!Shell_NotifyIconW(NIM_ADD, &m_nid)) {
        std::fprintf(stderr, "[TrayManager] NIM_ADD failed (err=%lu)\n",
                     GetLastError());
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    m_created = true;
    std::printf("[TrayManager] tray icon created (hwnd=%p)\n", (void*)m_hwnd);
    return true;
}

void TrayManager::shutdown()
{
    if (m_created) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_created = false;
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void TrayManager::setTooltip(const std::wstring& text)
{
    if (!m_created) return;
    wcscpy_s(m_nid.szTip, text.c_str());
    m_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

// ── WndProc ──────────────────────────────────────────────────────────

LRESULT CALLBACK TrayManager::WndProc(HWND hwnd, UINT msg,
                                       WPARAM wp, LPARAM lp)
{
    TrayManager* self = nullptr;

    // 从 GWLP_USERDATA 获取 this 指针（WM_CREATE 后可用）
    if (msg == WM_CREATE) {
        CREATESTRUCT* cs = (CREATESTRUCT*)lp;
        self = (TrayManager*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (TrayManager*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }

    if (!self) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    // Taskbar 重启 → 重建图标
    if (msg == self->m_taskbarRestartMsg && self->m_taskbarRestartMsg != 0) {
        Shell_NotifyIconW(NIM_ADD, &self->m_nid);
        return 0;
    }

    // 托盘图标消息
    if (msg == WM_TRAYICON) {
        switch (lp) {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            if (self->m_cb) self->m_cb(Event::ToggleVisible);
            return 0;

        case WM_RBUTTONUP:
            self->showContextMenu();
            return 0;

        default:
            break;
        }
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── 右键菜单 ────────────────────────────────────────────────────────

void TrayManager::showContextMenu()
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, 1, L"显示主窗口 (&S)");
    AppendMenuW(hMenu, MF_STRING, 3, L"设置 (&E)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 2, L"退出 (&X)");

    SetForegroundWindow(m_hwnd);
    POINT pt;
    GetCursorPos(&pt);
    UINT cmd = TrackPopupMenu(hMenu,
        TPM_RETURNCMD | TPM_NONOTIFY,
        pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);

    if (cmd == 1) {
        if (m_cb) m_cb(Event::ShowWindow);
    } else if (cmd == 3) {
        if (m_cb) m_cb(Event::OpenSettings);
    } else if (cmd == 2) {
        if (m_cb) m_cb(Event::Exit);
    }
}

} // namespace geofinder
