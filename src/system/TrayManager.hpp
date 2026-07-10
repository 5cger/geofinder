// 参考蓝图 §5.13 — TrayManager 系统托盘
//
// 使用独立隐藏消息窗口接收托盘通知，避免被 glfwPollEvents 吞掉消息。
// 左键单击切换窗口显隐，右键弹出菜单（显示/退出）。
#pragma once

#include <windows.h>
#include <functional>
#include <string>

namespace geofinder {

class TrayManager {
public:
    enum class Event {
        ToggleVisible,   // 左键单击
        Exit,            // 右键 → 退出
        ShowWindow,      // 右键 → 显示窗口
        OpenSettings,    // 右键 → 设置
    };

    using TrayCallback = std::function<void(Event)>;

    TrayManager();
    ~TrayManager();

    /// 创建托盘图标（内部创建隐藏消息窗口）
    bool init();

    /// 移除托盘图标 + 销毁隐藏窗口
    void shutdown();

    /// 设置事件回调
    void setCallback(TrayCallback cb) { m_cb = std::move(cb); }

    /// 更新提示文字
    void setTooltip(const std::wstring& text);

private:
    static constexpr UINT WM_TRAYICON = WM_APP + 0x100;
    static const wchar_t* kWindowClass;

    NOTIFYICONDATAW m_nid = {};
    bool m_created = false;
    HWND m_hwnd = nullptr;
    TrayCallback m_cb;
    UINT m_taskbarRestartMsg = 0;

    void showContextMenu();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                     WPARAM wp, LPARAM lp);
};

} // namespace geofinder
