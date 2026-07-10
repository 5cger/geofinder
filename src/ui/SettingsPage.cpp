// SettingsPage — 设置页面（完整版：扫描 + 动画速度 GUI 控制）
#include "ui/SettingsPage.hpp"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <algorithm>

namespace geofinder {

SettingsPage::SettingsPage(FontManager* fontMgr, ConfigManager* config)
    : m_fontMgr(fontMgr), m_config(config)
{
    m_size = glm::vec2(780.0f, 620.0f);
    refresh();
}

void SettingsPage::refresh()
{
    m_dirs.clear();
    if (m_config) m_dirs = m_config->getSearchDirs();
    if (m_selectedIndex >= static_cast<int>(m_dirs.size()))
        m_selectedIndex = std::max(0, static_cast<int>(m_dirs.size()) - 1);
    m_animSpeed = m_config ? m_config->getAnimSpeed() : 10.0f;
    m_cursorSpeed = m_config ? m_config->getCursorSpeed() : 14.0f;
}

const std::vector<std::string>& SettingsPage::getDirectories() const { return m_dirs; }

void SettingsPage::addDirectory(const std::wstring& dir)
{
    if (!m_config || dir.empty()) return;
    m_config->addSearchDir(StringUtils::wideToUtf8(dir));
    m_config->save();
    refresh();
}

void SettingsPage::removeSelected()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_dirs.size())) return;
    if (m_config) { m_config->removeSearchDir(m_dirs[m_selectedIndex]); m_config->save(); }
    refresh();
}

void SettingsPage::setScanProgress(int indexed, const std::wstring& currentDir)
{ m_scanProgress = indexed; m_scanCurrentDir = currentDir; }
void SettingsPage::setScanComplete(int totalIndexed)
{ m_scanning = false; m_scanTotal = totalIndexed; }
void SettingsPage::setScanning(bool scanning)
{ m_scanning = scanning; if (scanning) { m_scanProgress = 0; m_scanTotal = 0; } }

bool SettingsPage::handleInput(const std::wstring& text)
{
    if (text.empty()) return false;
    bool isPath = (text.size() >= 2 && text[1] == L':') || text[0] == L'\\';
    if (!isPath) return false;
    DWORD attr = GetFileAttributesW(text.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        m_statusMsg = L"⚠ 目录不存在: " + text; return true;
    }
    addDirectory(text); m_statusMsg = L"✓ 已添加: " + text;
    if (m_onAdd) m_onAdd();
    return true;
}

void SettingsPage::renderText(CommandBuffer& cmdBuf, const std::wstring& text,
                               float x, float y, float fontSize, const glm::vec4& color)
{
    if (!m_fontMgr || text.empty()) return;
    auto run = m_fontMgr->createTextRun(text, fontSize, 9999.0f, color);
    if (run.atlasHandle == kInvalidHandle || run.vertices.empty()) return;
    RenderCommand cmd; TextRunOp op;
    op.textureHandle = run.atlasHandle;
    op.vertices.resize(run.vertices.size());
    for (size_t i = 0; i < run.vertices.size(); ++i) {
        op.vertices[i].pos = glm::vec2(run.vertices[i].pos.x + x, run.vertices[i].pos.y + y);
        op.vertices[i].uv = run.vertices[i].uv;
    }
    op.indices = std::move(run.indices); op.color = color; op.opacity = 1.0f;
    cmd.op = op; cmdBuf.add(cmd);
}

static void drawRect(CommandBuffer& cmdBuf, glm::vec2 pos, glm::vec2 size, glm::vec4 color) {
    RenderCommand cmd; RectOp r; r.pos = pos; r.size = size; r.color = color; cmd.op = r; cmdBuf.add(cmd);
}

void SettingsPage::paint(CommandBuffer& cmdBuf)
{
    if (!m_visible) return;
    float px = m_pos.x + m_paddingX;
    float w = m_size.x - m_paddingX * 2;
    float y = m_pos.y + 16.0f;

    drawRect(cmdBuf, m_pos, m_size, glm::vec4(0.08f, 0.08f, 0.10f, 0.98f));

    // 标题
    renderText(cmdBuf, L"⚙ 设置", px, y, 20.0f, glm::vec4(0.3f, 0.7f, 1.0f, 1.0f));
    y += 36.0f;

    // ════ 扫描目录 ════
    drawRect(cmdBuf, glm::vec2(px, y), glm::vec2(w, 1), glm::vec4(0.2f, 0.2f, 0.25f, 1));
    y += 12.0f;
    renderText(cmdBuf, L"扫描目录 (" + std::to_wstring(m_dirs.size()) + L" 个)", px, y, 14.0f,
               glm::vec4(0.5f, 0.5f, 0.6f, 1.0f));
    y += 26.0f;

    int visibleCount = m_dirs.empty() ? 3 : std::min(static_cast<int>(m_dirs.size()), 6);
    if (m_dirs.empty()) {
        renderText(cmdBuf, L"（暂无自定义目录）", px + 4, y, 13.0f, glm::vec4(0.35f, 0.35f, 0.4f, 1));
        y += 28.0f;
    } else {
        int startIdx = 0;
        if (m_selectedIndex >= visibleCount) startIdx = m_selectedIndex - visibleCount + 1;
        for (int i = startIdx; i < static_cast<int>(m_dirs.size()) && (i - startIdx) < visibleCount; ++i) {
            bool sel = (i == m_selectedIndex && m_animFocusIdx == 0);
            if (sel) drawRect(cmdBuf, glm::vec2(px + 2, y - 1), glm::vec2(w - 8, m_itemHeight + 2),
                              glm::vec4(0.18f, 0.35f, 0.7f, 0.40f));
            std::wstring path(m_dirs[i].begin(), m_dirs[i].end());
            if (path.size() > 70) path = path.substr(0, 35) + L"..." + path.substr(path.size() - 30);
            renderText(cmdBuf, L"  " + path, px + 4, y + 2, 13.0f,
                       sel ? glm::vec4(1,1,1,1) : glm::vec4(0.65f, 0.65f, 0.7f, 1));
            if (sel) renderText(cmdBuf, L"[Del 删除]", px + w - 90, y + 2, 12.0f,
                                glm::vec4(0.95f, 0.35f, 0.35f, 1));
            y += m_itemHeight + 4;
        }
    }
    y += 8;
    renderText(cmdBuf, L"+ 输入路径（如 D:\\Tools）后按 Enter 添加", px+4, y+2, 12.0f,
               glm::vec4(0.4f, 0.5f, 0.6f, 1));
    y += 28;

    // ════ 扫描状态 ════
    drawRect(cmdBuf, glm::vec2(px, y), glm::vec2(w, 1), glm::vec4(0.2f, 0.2f, 0.25f, 1));
    y += 14;
    if (m_scanning) {
        float barY = y + 20, barH = 8;
        drawRect(cmdBuf, glm::vec2(px, barY), glm::vec2(w, barH), glm::vec4(0.15f, 0.15f, 0.18f, 1));
        float pct = m_scanTotal > 0 ? std::min(1.0f, (float)m_scanProgress / (float)m_scanTotal) : 0;
        if (pct > 0) drawRect(cmdBuf, glm::vec2(px, barY), glm::vec2(w * pct, barH),
                              glm::vec4(0.2f, 0.55f, 0.9f, 1));
        wchar_t pt[64]; _snwprintf(pt, 64, L"扫描中... %d 文件", m_scanProgress);
        renderText(cmdBuf, pt, px, y, 13.0f, glm::vec4(0.7f, 0.8f, 0.95f, 1));
        y = barY + barH + 10;
        if (!m_scanCurrentDir.empty()) {
            std::wstring cd = m_scanCurrentDir;
            if (cd.size() > 65) cd = cd.substr(0, 30) + L"..." + cd.substr(cd.size() - 32);
            renderText(cmdBuf, cd, px, y, 12.0f, glm::vec4(0.45f, 0.45f, 0.5f, 1));
        }
    } else if (m_scanTotal > 0) {
        wchar_t dt[64]; _snwprintf(dt, 64, L"✓ 扫描完成 — 共索引 %d 个文件", m_scanTotal);
        renderText(cmdBuf, dt, px, y, 13.0f, glm::vec4(0.3f, 0.8f, 0.4f, 1));
    } else {
        renderText(cmdBuf, L"按 Ctrl+R 或 [重新扫描] 开始扫描", px, y, 13.0f,
                   glm::vec4(0.45f, 0.45f, 0.5f, 1));
    }
    y += 28;

    // ════ 动画速度 ════
    drawRect(cmdBuf, glm::vec2(px, y), glm::vec2(w, 1), glm::vec4(0.2f, 0.2f, 0.25f, 1));
    y += 14;
    renderText(cmdBuf, L"动画速度", px, y, 14.0f, glm::vec4(0.5f, 0.5f, 0.6f, 1));
    y += 26;

    // 高亮条速度
    {
        bool sel = (m_animFocusIdx == 1);
        wchar_t line[80]; _snwprintf(line, 80, L"  高亮/滚动:  %.1f  [← 慢 / 快 →]", m_animSpeed);
        if (sel) drawRect(cmdBuf, glm::vec2(px, y - 2), glm::vec2(w, m_itemHeight),
                          glm::vec4(0.18f, 0.35f, 0.7f, 0.30f));
        renderText(cmdBuf, line, px + 4, y + 2, 13.0f,
                   sel ? glm::vec4(1,1,1,1) : glm::vec4(0.7f, 0.7f, 0.75f, 1));
        y += m_itemHeight + 4;
    }
    // 光标速度
    {
        bool sel = (m_animFocusIdx == 2);
        wchar_t line[80]; _snwprintf(line, 80, L"  光标滑动:  %.1f  [← 慢 / 快 →]", m_cursorSpeed);
        if (sel) drawRect(cmdBuf, glm::vec2(px, y - 2), glm::vec2(w, m_itemHeight),
                          glm::vec4(0.18f, 0.35f, 0.7f, 0.30f));
        renderText(cmdBuf, line, px + 4, y + 2, 13.0f,
                   sel ? glm::vec4(1,1,1,1) : glm::vec4(0.7f, 0.7f, 0.75f, 1));
        y += m_itemHeight + 4;
    }
    y += 8;

    // 状态消息
    if (!m_statusMsg.empty()) {
        renderText(cmdBuf, m_statusMsg, px, y, 13.0f, glm::vec4(1.0f, 0.85f, 0.3f, 1));
        y += 28;
    }

    // ════ 操作按钮 ════
    drawRect(cmdBuf, glm::vec2(px, y), glm::vec2(w, 1), glm::vec4(0.2f, 0.2f, 0.25f, 1));
    y += 14;

    float btnW = 100, btnH = 28;
    drawRect(cmdBuf, glm::vec2(px, y), glm::vec2(btnW, btnH), glm::vec4(0.18f, 0.38f, 0.65f, 0.80f));
    drawRect(cmdBuf, glm::vec2(px, y), glm::vec2(btnW, 1), glm::vec4(0.3f, 0.5f, 0.8f, 1));
    drawRect(cmdBuf, glm::vec2(px, y), glm::vec2(1, btnH), glm::vec4(0.3f, 0.5f, 0.8f, 1));
    drawRect(cmdBuf, glm::vec2(px, y + btnH - 1), glm::vec2(btnW, 1), glm::vec4(0.3f, 0.5f, 0.8f, 1));
    drawRect(cmdBuf, glm::vec2(px + btnW - 1, y), glm::vec2(1, btnH), glm::vec4(0.3f, 0.5f, 0.8f, 1));
    renderText(cmdBuf, L"  重新扫描", px + 4, y + 4, 13.0f, glm::vec4(0.9f, 0.9f, 1, 1));
    y += btnH + 12;

    renderText(cmdBuf, L"↑↓ 选择区域  ←→ 调速  Del 删除目录  Ctrl+R 重新扫描  Esc 返回",
               px, y, 12.0f, glm::vec4(0.35f, 0.4f, 0.5f, 1));
}

// ── 键盘事件 ──────────────────────────────────────────────────────

bool SettingsPage::onKeyEvent(const KeyEvent& evt)
{
    if (evt.action != GLFW_PRESS && evt.action != GLFW_REPEAT) return false;

    switch (evt.key) {
    case GLFW_KEY_ESCAPE:
        if (m_onBack) m_onBack(); return true;
    case GLFW_KEY_R:
        if (evt.mods & GLFW_MOD_CONTROL) { if (m_onRescan) m_onRescan(); return true; }
        return false;
    case GLFW_KEY_UP:
        if (m_animFocusIdx > 0) m_animFocusIdx--; return true;
    case GLFW_KEY_DOWN:
        if (m_animFocusIdx < 2) m_animFocusIdx++; return true;
    case GLFW_KEY_DELETE:
        if (m_animFocusIdx == 0) removeSelected();
        return true;
    case GLFW_KEY_LEFT: {
        if (m_animFocusIdx == 1 && m_animSpeed > 1.0f) {
            m_animSpeed = std::max(1.0f, m_animSpeed - 1.0f);
            if (m_config) { m_config->setAnimSpeed(m_animSpeed); m_config->save(); }
            if (m_onAnimSpeed) m_onAnimSpeed(m_animSpeed);
            m_statusMsg = L"高亮速度: " + std::to_wstring((int)m_animSpeed);
        }
        if (m_animFocusIdx == 2 && m_cursorSpeed > 1.0f) {
            m_cursorSpeed = std::max(1.0f, m_cursorSpeed - 1.0f);
            if (m_config) { m_config->setCursorSpeed(m_cursorSpeed); m_config->save(); }
            if (m_onCursorSpeed) m_onCursorSpeed(m_cursorSpeed);
            m_statusMsg = L"光标速度: " + std::to_wstring((int)m_cursorSpeed);
        }
        return true;
    }
    case GLFW_KEY_RIGHT: {
        if (m_animFocusIdx == 1 && m_animSpeed < 30.0f) {
            m_animSpeed = std::min(30.0f, m_animSpeed + 1.0f);
            if (m_config) { m_config->setAnimSpeed(m_animSpeed); m_config->save(); }
            if (m_onAnimSpeed) m_onAnimSpeed(m_animSpeed);
            m_statusMsg = L"高亮速度: " + std::to_wstring((int)m_animSpeed);
        }
        if (m_animFocusIdx == 2 && m_cursorSpeed < 30.0f) {
            m_cursorSpeed = std::min(30.0f, m_cursorSpeed + 1.0f);
            if (m_config) { m_config->setCursorSpeed(m_cursorSpeed); m_config->save(); }
            if (m_onCursorSpeed) m_onCursorSpeed(m_cursorSpeed);
            m_statusMsg = L"光标速度: " + std::to_wstring((int)m_cursorSpeed);
        }
        return true;
    }
    default: return false;
    }
}

} // namespace geofinder
