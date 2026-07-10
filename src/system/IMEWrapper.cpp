// 参考蓝图 §5.14 — IMEWrapper IMM32 实现

#include "system/IMEWrapper.hpp"
#include <cstdio>

namespace geofinder {

IMEWrapper::IMEWrapper() = default;

IMEWrapper::~IMEWrapper() = default;

bool IMEWrapper::handleImeMessage(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_IME_STARTCOMPOSITION:
        m_composing = true;
        m_preedit.clear();
        updateCompositionWindow();
        return true;

    case WM_IME_COMPOSITION: {
        if (!m_hwnd || !m_composing) return false;

        HIMC hImc = ImmGetContext(m_hwnd);
        if (!hImc) return false;

        std::wstring newPreedit;

        if (lp & GCS_COMPSTR) {
            LONG len = ImmGetCompositionStringW(hImc, GCS_COMPSTR, nullptr, 0);
            if (len > 0) {
                newPreedit.resize(len / sizeof(wchar_t));
                ImmGetCompositionStringW(hImc, GCS_COMPSTR,
                    (LPVOID)newPreedit.data(), len);
            }
        }

        ImmReleaseContext(m_hwnd, hImc);

        if (newPreedit != m_preedit) {
            m_preedit = newPreedit;
            if (m_cb) m_cb(m_preedit);
        }
        updateCompositionWindow();
        return true;
    }

    case WM_IME_ENDCOMPOSITION:
        m_composing = false;
        m_preedit.clear();
        if (m_cb) m_cb(L"");
        return true;

    default:
        return false;
    }
}

std::wstring IMEWrapper::commitComposition()
{
    std::wstring result = m_preedit;
    m_preedit.clear();
    m_composing = false;
    if (m_cb) m_cb(L"");
    return result;
}

void IMEWrapper::updateCompositionWindow()
{
    if (!m_hwnd) return;

    HIMC hImc = ImmGetContext(m_hwnd);
    if (hImc) {
        COMPOSITIONFORM cf = {};
        cf.dwStyle = CFS_POINT;
        cf.ptCurrentPos.x = 16;  // InputField padding
        cf.ptCurrentPos.y = 56;  // InputField height
        ImmSetCompositionWindow(hImc, &cf);
        ImmReleaseContext(m_hwnd, hImc);
    }
}

} // namespace geofinder
