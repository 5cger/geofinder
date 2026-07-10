// 参考蓝图 §5.14 — IMEWrapper 输入法封装
//
// IMM32 实现：处理 WM_IME_* 消息，提取预编辑文本，
// 通过回调传给 InputField 显示。
#pragma once

#include <windows.h>
#include <string>
#include <functional>

namespace geofinder {

class IMEWrapper {
public:
    using ImeCallback = std::function<void(const std::wstring& preedit)>;

    IMEWrapper();
    ~IMEWrapper();

    /// 设置 GLFW 窗口 HWND（用于 ImmSetCompositionWindow）
    void setWindow(HWND hwnd) { m_hwnd = hwnd; }

    /// 设置预编辑文本回调
    void setCallback(ImeCallback cb) { m_cb = std::move(cb); }

    /// 子类化窗口过程（在 WindowManager 的 WndProc 中调用）
    bool handleImeMessage(UINT msg, WPARAM wp, LPARAM lp);

    /// 查询 IME 是否正在组合中
    bool isComposing() const { return m_composing; }

    /// 提交预编辑文本（用户确认后返回最终字符串）
    std::wstring commitComposition();

private:
    HWND m_hwnd = nullptr;
    HIMC m_hImc = nullptr;
    bool m_composing = false;
    std::wstring m_preedit;
    ImeCallback m_cb;

    void updateCompositionWindow();
};

} // namespace geofinder
