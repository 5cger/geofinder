// 参考蓝图 §5.12 — HotkeyManager 实现

#include "system/HotkeyManager.hpp"

#include <cstdio>

namespace geofinder {

HotkeyManager::HotkeyManager() = default;

HotkeyManager::~HotkeyManager() {
    unregisterHotkey();
}

bool HotkeyManager::registerHotkey(const HotkeyConfig& config)
{
    if (m_registered) {
        unregisterHotkey();
    }

    if (!RegisterHotKey(nullptr, HOTKEY_ID,
                        config.modifiers, config.vk)) {
        std::fprintf(stderr, "[HotkeyManager] RegisterHotKey failed (err=%lu)\n",
                     GetLastError());
        return false;
    }

    m_config = config;
    m_registered = true;
    std::printf("[HotkeyManager] registered: mod=0x%x vk=0x%x\n",
                config.modifiers, config.vk);
    return true;
}

void HotkeyManager::unregisterHotkey()
{
    if (m_registered) {
        UnregisterHotKey(nullptr, HOTKEY_ID);
        m_registered = false;
    }
}

bool HotkeyManager::checkMessage(const MSG& msg)
{
    if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID) {
        if (m_cb) {
            m_cb();
        }
        return true;
    }
    return false;
}

bool HotkeyManager::tryRegister(const HotkeyConfig& config)
{
    if (!RegisterHotKey(nullptr, 0xF002, config.modifiers, config.vk)) {
        return false;
    }
    UnregisterHotKey(nullptr, 0xF002);
    return true;
}

} // namespace geofinder
