// ResultList — 每项独立淡入（仅数量变化时动画）
#include "ui/ResultList.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <unordered_set>
#include <cstdio>
#include <cmath>

namespace geofinder {

static constexpr float kAnimSpeed = 10.0f;

ResultList::ResultList(FontManager* fontMgr, IconCache* iconCache)
    : m_fontMgr(fontMgr), m_iconCache(iconCache)
{ m_size = glm::vec2(600.0f, 400.0f); }

void ResultList::update(float delta)
{
    float dt = std::min(delta, 0.1f);
    m_highlightAlpha += (m_highlightTarget - m_highlightAlpha) * kAnimSpeed * dt;
    m_highlightY += (m_highlightTargetY - m_highlightY) * kAnimSpeed * dt;
    m_scrollY += (m_scrollTargetY - m_scrollY) * kAnimSpeed * dt;
    for (auto& a : m_itemAlpha) a += (1.0f - a) * kAnimSpeed * dt;
}

void ResultList::setResults(const std::vector<SearchResultEntry>& results)
{
    int oldIdx = m_selectedIndex;
    bool countChanged = (results.size() != m_prevPaths.size());
    std::unordered_set<std::wstring> prevSet(m_prevPaths.begin(), m_prevPaths.end());

    m_itemAlpha.resize(results.size(), 0.0f);
    for (size_t i = 0; i < results.size(); ++i)
        m_itemAlpha[i] = (!countChanged || prevSet.count(results[i].path)) ? 1.0f : 0.0f;

    m_prevPaths.clear();
    for (const auto& r : results) m_prevPaths.push_back(r.path);

    m_results = results;
    if (m_results.empty()) m_selectedIndex = -1;
    else if (oldIdx < 0) m_selectedIndex = 0;
    else if (oldIdx >= (int)m_results.size()) m_selectedIndex = 0;
    else m_selectedIndex = oldIdx;

    m_scrollOffset = 0;
    m_scrollY = 0.0f; m_scrollTargetY = 0.0f;
    m_highlightAlpha = 1.0f; m_highlightTarget = 1.0f;
    m_highlightY = m_pos.y; m_highlightTargetY = m_pos.y;
}

void ResultList::setSelectedIndex(int index)
{
    int oldIndex = m_selectedIndex;
    m_selectedIndex = std::max(-1, std::min(index, (int)m_results.size() - 1));
    if (m_selectedIndex < 0) return;
    if (m_selectedIndex != oldIndex) { m_highlightAlpha = 0.4f; m_highlightTarget = 1.0f; }
    if (m_selectedIndex < m_scrollOffset) {
        m_scrollOffset = m_selectedIndex;
        m_scrollTargetY = (float)m_scrollOffset * m_itemHeight;
    } else if (m_selectedIndex >= m_scrollOffset + m_visibleItems) {
        m_scrollOffset = m_selectedIndex - m_visibleItems + 1;
        m_scrollTargetY = (float)m_scrollOffset * m_itemHeight;
    }
    m_highlightTargetY = m_pos.y + (float)(m_selectedIndex - m_scrollOffset) * m_itemHeight;
    if (oldIndex < 0) { m_highlightY = m_highlightTargetY; m_scrollY = m_scrollTargetY; }
    if (m_selectedIndex != oldIndex && m_onSelection) m_onSelection(m_selectedIndex);
}

const SearchResultEntry* ResultList::getSelectedResult() const {
    if (m_selectedIndex < 0 || m_selectedIndex >= (int)m_results.size()) return nullptr;
    return &m_results[m_selectedIndex];
}

bool ResultList::onMouseEvent(const MouseEvent& evt) {
    if (!m_visible || !hitTest(evt.pos)) return false;
    if (evt.type == MouseEvent::Press && evt.button == 0) {
        float localY = evt.pos.y - m_pos.y;
        int idx = (int)((localY + m_scrollY) / m_itemHeight);
        if (idx >= 0 && idx < (int)m_results.size()) { setSelectedIndex(idx); return true; }
    }
    return false;
}

bool ResultList::onKeyEvent(const KeyEvent& evt) {
    if (evt.action != GLFW_PRESS && evt.action != GLFW_REPEAT) return false;
    switch (evt.key) {
    case GLFW_KEY_UP: if (m_selectedIndex > 0) setSelectedIndex(m_selectedIndex-1); return true;
    case GLFW_KEY_DOWN: if (m_selectedIndex < (int)m_results.size()-1) setSelectedIndex(m_selectedIndex+1); return true;
    case GLFW_KEY_PAGE_UP: setSelectedIndex(std::max(0, m_selectedIndex - m_visibleItems)); return true;
    case GLFW_KEY_PAGE_DOWN: setSelectedIndex(std::min((int)m_results.size()-1, m_selectedIndex + m_visibleItems)); return true;
    case GLFW_KEY_HOME: setSelectedIndex(0); return true;
    case GLFW_KEY_END: setSelectedIndex((int)m_results.size()-1); return true;
    default: return false;
    }
}

static std::wstring truncatePath(const std::wstring& path, size_t maxLen = 60) {
    if (path.size() <= maxLen) return path;
    size_t keep = (maxLen - 3) / 2;
    return path.substr(0, keep) + L"..." + path.substr(path.size() - keep);
}

void ResultList::paint(CommandBuffer& cmdBuf) {
    if (!m_visible) return;
    { RenderCommand c; RectOp r; r.pos = m_pos; r.size = m_size; r.color = glm::vec4(0.08f,0.08f,0.08f,0.95f); c.op = r; cmdBuf.add(c); }

    if (m_results.empty()) {
        if (!m_statusText.empty() && m_fontMgr) {
            float fontSize = 12.0f;
            auto run = m_fontMgr->createTextRun(m_statusText, fontSize, m_size.x - m_paddingX * 2, glm::vec4(0.45f,0.45f,0.45f,1));
            if (run.atlasHandle != kInvalidHandle && !run.vertices.empty()) {
                float oy = m_pos.y + m_size.y - fontSize - 8.0f;
                RenderCommand c; TextRunOp t; t.textureHandle = run.atlasHandle; t.vertices.resize(run.vertices.size());
                for (size_t v = 0; v < run.vertices.size(); ++v) { t.vertices[v].pos = glm::vec2(run.vertices[v].pos.x + m_pos.x + m_paddingX, run.vertices[v].pos.y + oy); t.vertices[v].uv = run.vertices[v].uv; }
                t.indices = std::move(run.indices); t.color = glm::vec4(0.45f,0.45f,0.45f,1); t.opacity = 1;
                c.op = t; cmdBuf.add(c);
            }
        }
        return;
    }

    int startIdx = m_scrollOffset;
    int endIdx = std::min(startIdx + m_visibleItems + 1, (int)m_results.size());

    for (int i = startIdx; i < endIdx; ++i) {
        const auto& result = m_results[i];
        float itemY = m_pos.y + (float)i * m_itemHeight - m_scrollY;
        bool sel = (i == m_selectedIndex);
        float itemAlpha = (i < (int)m_itemAlpha.size()) ? m_itemAlpha[i] : 1.0f;

        if (sel) {
            float hlY = m_highlightY;
            float hlAlpha = 0.4f * m_highlightAlpha * itemAlpha;
            { RenderCommand c; RectOp r; r.pos = glm::vec2(m_pos.x+2, hlY+1); r.size = glm::vec2(m_size.x-4, m_itemHeight-2);
              r.color = glm::vec4(0.25f,0.45f,0.85f,hlAlpha); c.op = r; cmdBuf.add(c); }
            { RenderCommand c; RectOp r; r.pos = glm::vec2(m_pos.x+2, hlY); r.size = glm::vec2(3, m_itemHeight);
              r.color = glm::vec4(0.3f,0.6f,1.0f,m_highlightAlpha*itemAlpha); c.op = r; cmdBuf.add(c); }
        }
        if (itemY + m_itemHeight < m_pos.y || itemY > m_pos.y + m_size.y) continue;

        float iconX = m_pos.x + m_paddingX;
        float iconY = itemY + (m_itemHeight - m_iconSize) * 0.5f;
        if (m_iconCache) {
            ResourceHandle h = m_iconCache->getFileIcon(result.path, (int)m_iconSize);
            if (h != kInvalidHandle) { RenderCommand c; IconOp ic; ic.textureHandle = h; ic.pos = glm::vec2(iconX, iconY); ic.size = glm::vec2(m_iconSize); ic.tintColor = glm::vec4(1,1,1,itemAlpha); c.op = ic; cmdBuf.add(c); }
        }
        float textX = iconX + m_iconSize + 8.0f;

        if (m_fontMgr && !result.name.empty()) {
            float maxW = m_size.x - textX - m_paddingX - 80;
            auto run = m_fontMgr->createTextRun(result.name, m_fontSize, maxW, glm::vec4(1,1,1,itemAlpha));
            if (run.atlasHandle != kInvalidHandle && !run.vertices.empty()) {
                float oy = itemY + m_fontSize + 2;
                RenderCommand c; TextRunOp t; t.textureHandle = run.atlasHandle; t.vertices.resize(run.vertices.size());
                for (size_t v = 0; v < run.vertices.size(); ++v) { t.vertices[v].pos = glm::vec2(run.vertices[v].pos.x + textX, run.vertices[v].pos.y + oy - m_fontSize); t.vertices[v].uv = run.vertices[v].uv; }
                t.indices = std::move(run.indices); t.color = sel ? glm::vec4(1,1,1,itemAlpha) : glm::vec4(0.8f,0.8f,0.8f,itemAlpha); t.opacity = itemAlpha;
                c.op = t; cmdBuf.add(c);
            }
        }
        {
            std::wstring info = truncatePath(result.path, 60);
            if (result.fileSize > 0) {
                wchar_t sz[32];
                if (result.fileSize < 1024) _snwprintf(sz, 32, L"  %llu B", result.fileSize);
                else if (result.fileSize < 1024*1024) _snwprintf(sz, 32, L"  %.1f KB", result.fileSize/1024.0);
                else _snwprintf(sz, 32, L"  %.1f MB", result.fileSize/(1024.0*1024.0));
                info += sz;
            }
            float pfs = 12.0f; float maxW = m_size.x - textX - m_paddingX;
            auto run = m_fontMgr->createTextRun(info, pfs, maxW, glm::vec4(0.55f,0.55f,0.55f,itemAlpha));
            if (run.atlasHandle != kInvalidHandle && !run.vertices.empty()) {
                float oy = itemY + pfs + 24;
                RenderCommand c; TextRunOp t; t.textureHandle = run.atlasHandle; t.vertices.resize(run.vertices.size());
                for (size_t v = 0; v < run.vertices.size(); ++v) { t.vertices[v].pos = glm::vec2(run.vertices[v].pos.x + textX, run.vertices[v].pos.y + oy - pfs); t.vertices[v].uv = run.vertices[v].uv; }
                t.indices = std::move(run.indices); t.color = glm::vec4(0.55f,0.55f,0.55f,itemAlpha); t.opacity = itemAlpha;
                c.op = t; cmdBuf.add(c);
            }
        }
    }
    if (!m_statusText.empty() && m_fontMgr) {
        float fontSize = 12.0f;
        auto run = m_fontMgr->createTextRun(m_statusText, fontSize, m_size.x - m_paddingX * 2, glm::vec4(0.45f,0.45f,0.45f,1));
        if (run.atlasHandle != kInvalidHandle && !run.vertices.empty()) {
            float oy = m_pos.y + m_size.y - fontSize - 8.0f;
            RenderCommand c; TextRunOp t; t.textureHandle = run.atlasHandle; t.vertices.resize(run.vertices.size());
            for (size_t v = 0; v < run.vertices.size(); ++v) { t.vertices[v].pos = glm::vec2(run.vertices[v].pos.x + m_pos.x + m_paddingX, run.vertices[v].pos.y + oy); t.vertices[v].uv = run.vertices[v].uv; }
            t.indices = std::move(run.indices); t.color = glm::vec4(0.45f,0.45f,0.45f,1); t.opacity = 1;
            c.op = t; cmdBuf.add(c);
        }
    }
}

} // namespace geofinder
