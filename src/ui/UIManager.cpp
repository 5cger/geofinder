// 参考蓝图 §5.7 — UIManager 实现
//
// 支持搜索页 / 设置页切换。

#include "ui/UIManager.hpp"
#include <GLFW/glfw3.h>
#include <cstdio>

namespace geofinder {

UIManager::UIManager(FontManager* fontMgr, ConfigManager* config,
                     IconCache* iconCache, float dpiScale)
    : m_fontMgr(fontMgr)
    , m_dpiScale(dpiScale)
{
    m_inputField = new InputField(m_fontMgr);
    m_resultList = new ResultList(m_fontMgr, iconCache);
    m_settingsPage = std::make_unique<SettingsPage>(m_fontMgr, config);

    m_resultList->setOnConfirmCallback([this](int /*index*/) {
        const auto* result = m_resultList->getSelectedResult();
        if (result && m_launchCb) m_launchCb(*result);
    });
    m_resultList->setOnSelectionCallback([this](int /*index*/) { markDirty(); });

    m_settingsPage->setOnBack([this]() { showSearchPage(); });

    layoutPage();
}

UIManager::~UIManager()
{
    delete m_resultList;
    delete m_inputField;
}

// ── 页面管理 ──────────────────────────────────────────────────────

void UIManager::showSearchPage()
{
    m_currentPage = Page::Search;
    m_inputField->clear();
    m_inputField->setPlaceholder(L"输入搜索关键词...");
    m_inputField->setFocused(true);
    m_resultList->setResults({});
    markDirty();
    layoutPage();
}

void UIManager::showSettingsPage()
{
    m_currentPage = Page::Settings;
    m_settingsPage->refresh();
    m_inputField->clear();
    m_inputField->setPlaceholder(L"输入目录路径，按 Enter 添加...");
    m_inputField->setFocused(true);
    m_resultList->setResults({});
    markDirty();
    layoutPage();
}

// ── 更新 ──────────────────────────────────────────────────────────

void UIManager::update(float delta)
{
    m_anim.update(delta);
    m_resultList->update(delta);
    bool cursorChanged = m_inputField->update(delta);
    if (m_anim.hasActive() || cursorChanged) markDirty();
}

// ── 绘制 ──────────────────────────────────────────────────────────

void UIManager::paint(CommandBuffer& cmdBuf)
{
    if (m_currentPage == Page::Settings) {
        m_settingsPage->paint(cmdBuf);
        // 设置页模式下也绘制输入框（置于设置页上方）
        if (m_inputField && m_inputField->isVisible()) {
            m_inputField->paint(cmdBuf);
        }
    } else {
        Widget* root = getActiveRoot();
        if (!root) return;
        root->paint(cmdBuf);
    }
    m_dirty = false;
}

// ── 事件分发 ─────────────────────────────────────────────────────

bool UIManager::onMouseEvent(const MouseEvent& evt)
{
    if (m_currentPage == Page::Settings) return false;

    if (m_inputField && m_inputField->isVisible() && m_inputField->hitTest(evt.pos)) {
        m_inputField->setFocused(true);
        return m_inputField->onMouseEvent(evt);
    }
    if (m_resultList && m_resultList->isVisible() && m_resultList->hitTest(evt.pos)) {
        m_inputField->setFocused(false);
        bool handled = m_resultList->onMouseEvent(evt);
        if (handled) markDirty();
        return handled;
    }
    return false;
}

bool UIManager::onKeyEvent(const KeyEvent& evt)
{
    if (evt.action != GLFW_PRESS && evt.action != GLFW_REPEAT)
        return false;

    // ── 设置页键盘 ──────────────────────────────────────────
    if (m_currentPage == Page::Settings) {
        // Enter: 内联输入路径添加
        if (evt.key == GLFW_KEY_ENTER) {
            std::wstring text = m_inputField->getText();
            if (m_settingsPage && !text.empty()) {
                if (m_settingsPage->handleInput(text)) {
                    m_inputField->clear();
                    markDirty();
                    return true;
                }
                // 不是路径 → 传回搜索
                if (m_searchCb) m_searchCb(text);
                m_inputField->clear();
                return true;
            }
            return false;
        }
        // 其他键转发到 SettingsPage
        if (m_settingsPage && m_settingsPage->onKeyEvent(evt)) {
            markDirty();
            return true;
        }
        // 字符输入：让 onCharEvent 处理
        return false;
    }

    // ── 搜索页键盘 ──────────────────────────────────────────
    if (m_inputField && m_inputField->isFocused()) {
        if (m_inputField->onKeyEvent(evt)) { markDirty(); return true; }
    }

    if (evt.key == GLFW_KEY_DOWN || evt.key == GLFW_KEY_UP) {
        if (m_resultList && m_resultList->isVisible()) {
            bool handled = m_resultList->onKeyEvent(evt);
            if (handled) markDirty();
            return handled;
        }
    }

    if (evt.key == GLFW_KEY_ENTER) {
        const auto* result = m_resultList->getSelectedResult();
        if (result && m_launchCb) { m_launchCb(*result); return true; }
        return true;
    }

    if (evt.key == GLFW_KEY_ESCAPE) {
        if (!m_inputField->getText().empty()) {
            m_inputField->clear();
            markDirty();
            if (m_searchCb) m_searchCb(L"");
            return true;
        }
        return false;
    }

    return false;
}

bool UIManager::onCharEvent(const CharEvent& evt)
{
    if (m_inputField && m_inputField->isFocused()) {
        bool handled = m_inputField->onCharEvent(evt);
        if (handled) markDirty();
        return handled;
    }
    return false;
}

// ── 输入框 / 结果列表 ────────────────────────────────────────────

void UIManager::setSearchQuery(const std::wstring& query) {
    m_inputField->setText(query); markDirty();
}
const std::wstring& UIManager::getSearchQuery() const {
    return m_inputField->getText();
}
void UIManager::setSearchResults(const std::vector<SearchResultEntry>& results) {
    m_resultList->setResults(results); markDirty();
}
void UIManager::setStatusText(const std::wstring& text) {
    m_resultList->setStatusText(text); markDirty();
}
const SearchResultEntry* UIManager::getSelectedResult() const {
    return m_resultList->getSelectedResult();
}
void UIManager::setInputOnChangeCallback(std::function<void(const std::wstring&)> cb) {
    m_inputField->setOnChangeCallback(std::move(cb));
}
void UIManager::setDpiScale(float scale) {
    m_dpiScale = scale; layoutPage(); markDirty();
}

void UIManager::setSettingsRescanCallback(std::function<void()> cb) {
    if (m_settingsPage) m_settingsPage->setOnRescan(std::move(cb));
}

// ── 内部辅助 ─────────────────────────────────────────────────────

Widget* UIManager::getActiveRoot()
{
    struct SearchContainer : public Widget {
        InputField* input = nullptr;
        ResultList* results = nullptr;
        void paint(CommandBuffer& cmdBuf) override {
            if (input && input->isVisible()) input->paint(cmdBuf);
            if (results && results->isVisible()) results->paint(cmdBuf);
        }
    };

    if (!m_searchRoot) {
        auto* c = new SearchContainer();
        c->input = m_inputField;
        c->results = m_resultList;
        c->layout(m_layout.getWindowSize(), m_layout.getWindowSize());
        m_searchRoot.reset(c);
    }
    return m_searchRoot.get();
}

void UIManager::layoutPage()
{
    float w = m_layout.getWindowSize().x;
    float h = m_layout.getWindowSize().y;

    if (m_currentPage == Page::Settings) {
        // InputField 在顶部（用于输入目录路径）
        float ifH = 50.0f * m_dpiScale;
        m_inputField->layout(glm::vec2(0, 0), glm::vec2(w, ifH));
        // SettingsPage 在输入框下方
        m_settingsPage->layout(glm::vec2(0, ifH), glm::vec2(w, h - ifH));
    } else {
        if (m_inputField)
            m_inputField->layout(glm::vec2(0, 0), glm::vec2(w, 56.0f * m_dpiScale));
        if (m_resultList) {
            float top = m_inputField ? m_inputField->getSize().y : 56.0f * m_dpiScale;
            m_resultList->layout(glm::vec2(0, top), glm::vec2(w, std::max(0.0f, h - top)));
        }
    }
}

} // namespace geofinder
