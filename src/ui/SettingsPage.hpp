// SettingsPage — 设置页面（GUI 增强版：进度条 + 内联输入 + 状态反馈）
#pragma once

#include "ui/Widget.hpp"
#include "resource/FontManager.hpp"
#include "data/ConfigManager.hpp"
#include "utils/StringUtils.hpp"
#include <string>
#include <vector>
#include <functional>
#include <windows.h>

namespace geofinder {

class SettingsPage : public Widget {
public:
    SettingsPage(FontManager* fontMgr, ConfigManager* config);

    void paint(CommandBuffer& cmdBuf) override;
    bool onKeyEvent(const KeyEvent& evt) override;

    // 目录管理
    void addDirectory(const std::wstring& dir);
    void removeSelected();
    void refresh();

    int getSelectedIndex() const { return m_selectedIndex; }
    const std::vector<std::string>& getDirectories() const;

    // 内联输入：在设置页搜索框输入路径后按 Enter 添加
    // 返回 true 表示输入已被处理（不要传给搜索页）
    bool handleInput(const std::wstring& text);

    // 扫描进度控制
    void setScanProgress(int indexed, const std::wstring& currentDir);
    void setScanComplete(int totalIndexed);
    void setScanning(bool scanning);

    // 回调
    using ActionCallback = std::function<void()>;
    using SpeedCallback = std::function<void(float)>;
    void setOnRescan(ActionCallback cb) { m_onRescan = std::move(cb); }
    void setOnAdd(ActionCallback cb) { m_onAdd = std::move(cb); }
    void setOnBack(ActionCallback cb) { m_onBack = std::move(cb); }
    void setOnAnimSpeedChange(SpeedCallback cb) { m_onAnimSpeed = std::move(cb); }
    void setOnCursorSpeedChange(SpeedCallback cb) { m_onCursorSpeed = std::move(cb); }

private:
    FontManager* m_fontMgr;
    ConfigManager* m_config;

    std::vector<std::string> m_dirs;
    int m_selectedIndex = 0;

    // 扫描进度
    bool m_scanning = false;
    int m_scanProgress = 0;
    int m_scanTotal = 0;
    std::wstring m_scanCurrentDir;

    // 状态反馈
    std::wstring m_statusMsg;

    ActionCallback m_onRescan;
    ActionCallback m_onAdd;
    ActionCallback m_onBack;
    SpeedCallback m_onAnimSpeed;
    SpeedCallback m_onCursorSpeed;

    // 动画速度值
    float m_animSpeed = 10.0f;
    float m_cursorSpeed = 14.0f;
    int m_animFocusIdx = 0;  // 0=目录列表, 1=动画速度, 2=光标速度

    float m_fontSize = 16.0f;
    float m_itemHeight = 24.0f;
    float m_paddingX = 20.0f;

    void renderText(CommandBuffer& cmdBuf, const std::wstring& text,
                    float x, float y, float fontSize,
                    const glm::vec4& color);
};

} // namespace geofinder
