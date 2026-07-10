// ResultList — 搜索结果列表控件（动画增强版）
#pragma once

#include "ui/Widget.hpp"
#include "data/DataTypes.hpp"
#include "resource/FontManager.hpp"
#include "resource/IconCache.hpp"
#include <vector>
#include <string>

namespace geofinder {

class ResultList : public Widget {
public:
    ResultList(FontManager* fontMgr, IconCache* iconCache);

    // Widget 接口
    void paint(CommandBuffer& cmdBuf) override;
    bool onKeyEvent(const KeyEvent& evt) override;
    bool onMouseEvent(const MouseEvent& evt) override;

    // 动画更新（每帧调用）
    void update(float delta);

    // 结果数据
    void setResults(const std::vector<SearchResultEntry>& results);
    int getResultCount() const { return static_cast<int>(m_results.size()); }

    // 选中项
    int getSelectedIndex() const { return m_selectedIndex; }
    void setSelectedIndex(int index);
    const SearchResultEntry* getSelectedResult() const;

    // 回调
    using OnSelectionCallback = std::function<void(int index)>;
    void setOnSelectionCallback(OnSelectionCallback cb) { m_onSelection = std::move(cb); }
    using OnConfirmCallback = std::function<void(int index)>;
    void setOnConfirmCallback(OnConfirmCallback cb) { m_onConfirm = std::move(cb); }

    // 状态文字
    void setStatusText(const std::wstring& text) { m_statusText = text; }

    // 动画速度设置
    void setAnimSpeed(float speed) { m_animSpeed = speed; }

private:
    FontManager* m_fontMgr;
    IconCache* m_iconCache;

    std::vector<SearchResultEntry> m_results;
    int m_selectedIndex = -1;

    // 滚动（平滑动画）
    int m_scrollOffset = 0;
    float m_scrollY = 0.0f;          // 当前动画滚动位置（像素）
    float m_scrollTargetY = 0.0f;    // 目标滚动位置
    int m_visibleItems = 8;

    OnSelectionCallback m_onSelection;
    OnConfirmCallback m_onConfirm;
    std::wstring m_statusText;

    // 选中高亮动画
    float m_highlightAlpha = 1.0f;
    float m_highlightTarget = 1.0f;
    float m_highlightY = 0.0f;       // 高亮条 Y 位置（用于平滑移动）
    float m_highlightTargetY = 0.0f;

    // 样式
    float m_fontSize = 16.0f;
    float m_itemHeight = 44.0f;
    float m_iconSize = 24.0f;
    float m_paddingX = 16.0f;
    float m_animSpeed = 10.0f;
    // 每项独立透明度（新项淡入，旧项保留）
    std::vector<float> m_itemAlpha;
    std::vector<std::wstring> m_prevPaths;  // 上次结果路径列表
};

} // namespace geofinder
