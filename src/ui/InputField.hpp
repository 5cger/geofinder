// 参考蓝图 §5.7 — InputField 输入框控件
//
// 单行文本输入框，支持光标移动、退格删除、字符输入。
// 在搜索页面中作为主输入区域，接收键盘焦点。
//
// TODO(Sprint10): IME 预编辑文本显示集成
#pragma once

#include "ui/Widget.hpp"
#include "resource/FontManager.hpp"
#include <string>

namespace geofinder {

class InputField : public Widget {
public:
    InputField(FontManager* fontMgr);

    // Widget 接口
    void paint(CommandBuffer& cmdBuf) override;
    bool onKeyEvent(const KeyEvent& evt) override;
    bool onCharEvent(const CharEvent& evt) override;

    // 输入内容
    void setText(const std::wstring& text);
    const std::wstring& getText() const { return m_text; }
    void clear();

    // 占位文字
    void setPlaceholder(const std::wstring& text) { m_placeholder = text; }
    const std::wstring& getPlaceholder() const { return m_placeholder; }

    // 光标
    int getCursorPos() const { return m_cursorPos; }
    void setCursorPos(int pos);

    // 焦点
    void setFocused(bool focused) { m_focused = focused; }
    bool isFocused() const { return m_focused; }

    // 每帧更新（光标闪烁等），返回 true 表示需要重绘
    bool update(float delta);
    void syncCursorTargetX();

    // 输入内容变更回调（用于触发搜索防抖）
    using OnChangeCallback = std::function<void(const std::wstring&)>;
    void setOnChangeCallback(OnChangeCallback cb) { m_onChange = std::move(cb); }

private:
    FontManager* m_fontMgr;

    std::wstring m_text;
    std::wstring m_placeholder;
    int m_cursorPos = 0;
    bool m_focused = true;

    OnChangeCallback m_onChange;

    // 光标闪烁（平滑 alpha）+ 水平滑动动画
    float m_cursorTimer = 0.0f;
    float m_cursorAlpha = 1.0f;
    float m_cursorVisualX = 0.0f;    // 光标视觉 X 位置（动画）
    float m_cursorTargetX = 0.0f;    // 光标目标 X 位置

    // 样式（后续从主题配置读取）
    float m_fontSize = 24.0f;
    float m_paddingX = 16.0f;
    float m_paddingY = 12.0f;
};

} // namespace geofinder
