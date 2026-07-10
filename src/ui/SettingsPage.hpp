// SettingsPage — 预计算布局 + 高亮实时跟随（参照 ResultList）
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

class InputField;  // forward

class SettingsPage : public Widget {
public:
    SettingsPage(FontManager* fm, ConfigManager* cfg);

    void paint(CommandBuffer& cb) override;
    bool onKeyEvent(const KeyEvent& evt) override;
    void update(float dt);
    bool isAnimating() const;
    void refresh();
    bool isInSection() const { return m_inSection; }

    void setInputField(InputField* f) { m_inputField = f; }
    void setScanProgress(int n, const std::wstring& dir);
    void setScanComplete(int total);
    void setScanning(bool s);

    using ActionCallback = std::function<void()>;
    using SpeedCallback = std::function<void(float)>;
    void setOnRescan(ActionCallback cb) { m_onRescan = std::move(cb); }
    void setOnBack(ActionCallback cb) { m_onBack = std::move(cb); }
    void setOnAnimSpeedChange(SpeedCallback cb) { m_onAnimSpeed = std::move(cb); }

private:
    FontManager* m_fm;
    ConfigManager* m_cfg;
    InputField* m_inputField = nullptr;
    std::vector<std::string> m_dirs;

    static constexpr int kCards = 3;
    static constexpr float kCardH = 56, kGap = 8, kExpandH = 150, kSpd = 12;
    static constexpr float kPadX = 24;

    void recalcLayout();
    int m_focusIdx = 0;
    int m_selDir = 0;
    bool m_inSection = false;

    float m_cardY[3] = {};
    float m_expandH[3] = {};
    float m_expandT[3] = {};
    float m_hlY = 0, m_hlTgt = 0;
    float m_hlAlpha = 1.0f;
    float m_hlScale = 1.0f, m_hlScaleTgt = 1.0f;

    bool m_scanning = false;
    int m_scanProg = 0, m_scanTotal = 0;
    std::wstring m_scanDir;

    float m_as = 10, m_cs = 14;
    int m_subFocus = 0;

    ActionCallback m_onRescan, m_onBack;
    SpeedCallback m_onAnimSpeed;

    void renderText(CommandBuffer& cb, const std::wstring& t,
                    float x, float y, float sz, const glm::vec4& c);
    void setFocus(int idx);
};

} // namespace geofinder
