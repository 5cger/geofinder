// 参考蓝图 §5.12 — HotkeyManager 全局热键
//
// 通过 RegisterHotKey 注册全局热键（默认 Alt+Space）。
// 热键消息在主循环的 PeekMessage 中检查。
#pragma once

#include <windows.h>
#include <functional>

namespace geofinder {

struct HotkeyConfig {
    UINT modifiers = MOD_ALT;
    UINT vk = VK_SPACE;  // Alt+Space
};

class HotkeyManager {
public:
    HotkeyManager();
    ~HotkeyManager();

    /// 注册全局热键
    bool registerHotkey(const HotkeyConfig& config);
    void unregisterHotkey();

    /// 检查消息是否为热键（在主循环 PeekMessage 后调用）
    /// @return true 表示热键被触发
    bool checkMessage(const MSG& msg);

    /// 热键触发回调
    using HotkeyCallback = std::function<void()>;
    void setCallback(HotkeyCallback cb) { m_cb = std::move(cb); }

    /// 尝试注册热键（用于设置面板测试是否冲突）
    static bool tryRegister(const HotkeyConfig& config);

private:
    static constexpr int HOTKEY_ID = 0xF001;
    HotkeyConfig m_config;
    bool m_registered = false;
    HotkeyCallback m_cb;
};

} // namespace geofinder
