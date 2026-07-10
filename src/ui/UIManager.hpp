// 参考蓝图 §5.7 — UIManager UI 管理器
//
// 控件树所有者、事件分发中心、页面管理。
// 生命周期：Application 在 init() 中创建，在 shutdown() 中销毁。
//
// 事件路由：
//   GLFW 回调 → Application → UIManager::on{Char,Key,Mouse}Event
//   → 命中测试（鼠标）→ 焦点 Widget（键盘）
#pragma once

#include "ui/Widget.hpp"
#include "ui/InputField.hpp"
#include "ui/ResultList.hpp"
#include "ui/SettingsPage.hpp"
#include "ui/AnimationEngine.hpp"
#include "ui/LayoutEngine.hpp"
#include "resource/FontManager.hpp"
#include "data/DataTypes.hpp"
#include "data/ConfigManager.hpp"
#include <memory>
#include <vector>
#include <functional>

namespace geofinder {

class UIManager {
public:
    UIManager(FontManager* fontMgr, ConfigManager* config,
             IconCache* iconCache, float dpiScale = 1.0f);
    ~UIManager();

    // 页面切换
    void showSearchPage();
    void showSettingsPage();
    bool isSettingsPage() const { return m_currentPage == Page::Settings; }

    // 更新动画
    void update(float delta);

    // 生成绘制命令
    void paint(CommandBuffer& cmdBuf);

    // 事件分发
    bool onMouseEvent(const MouseEvent& evt);
    bool onKeyEvent(const KeyEvent& evt);
    bool onCharEvent(const CharEvent& evt);

    // 输入框内容
    void setSearchQuery(const std::wstring& query);
    const std::wstring& getSearchQuery() const;

    // 结果列表
    void setSearchResults(const std::vector<SearchResultEntry>& results);
    const SearchResultEntry* getSelectedResult() const;

    // 状态文字（结果为空时显示）
    void setStatusText(const std::wstring& text);

    // 标记需要重绘
    void markDirty() { m_dirty = true; }
    bool isDirty() const { return m_dirty; }
    void clearDirty() { m_dirty = false; }

    // DPI
    void setDpiScale(float scale);
    float getDpiScale() const { return m_dpiScale; }

    // 动画
    AnimationEngine& getAnimation() { return m_anim; }

    // 设置页
    SettingsPage* getSettingsPage() { return m_settingsPage.get(); }

    // 结果列表（用于设置动画速度等）
    ResultList* getResultList() { return m_resultList; }

    // 搜索回调（由 Application 设置）
    using SearchCallback = std::function<void(const std::wstring&)>;
    void setSearchCallback(SearchCallback cb) { m_searchCb = std::move(cb); }

    // 启动回调
    using LaunchCallback = std::function<void(const SearchResultEntry&)>;
    void setLaunchCallback(LaunchCallback cb) { m_launchCb = std::move(cb); }

    // 输入框内容变更回调（连接 Application 的搜索防抖）
    void setInputOnChangeCallback(std::function<void(const std::wstring&)> cb);

    // 设置页回调
    void setSettingsRescanCallback(std::function<void()> cb);

private:
    FontManager* m_fontMgr;
    float m_dpiScale;

    // 页面 Widget — 搜索页根 Widget 作为容器
    std::unique_ptr<Widget> m_searchRoot;

    // 子控件引用
    InputField* m_inputField = nullptr;
    ResultList* m_resultList = nullptr;

    // 设置页
    std::unique_ptr<SettingsPage> m_settingsPage;

    // 状态
    enum class Page { Search, Settings } m_currentPage = Page::Search;
    bool m_dirty = true;

    AnimationEngine m_anim;
    LayoutEngine m_layout;

    SearchCallback m_searchCb;
    LaunchCallback m_launchCb;

    Widget* getActiveRoot();
    void layoutPage();
};

} // namespace geofinder
