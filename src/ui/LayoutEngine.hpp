// 参考蓝图 §5.8 — LayoutEngine 布局引擎
//
// 第一版使用固定坐标布局（输入框固定在顶部，结果列表填充剩余空间），
// 后续可扩展流式布局。
//
// TODO(Sprint11): 根据 ConfigPage 设置动态调整 padding/spacing
#pragma once

#include "ui/Widget.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace geofinder {

class LayoutEngine {
public:
    // 垂直排列子 Widget
    void layoutVertical(Widget* parent,
                        const std::vector<Widget*>& children,
                        float padding, float spacing);

    // 水平排列
    void layoutHorizontal(Widget* parent,
                          const std::vector<Widget*>& children,
                          float padding, float spacing);

    // 居中
    static void centerInParent(Widget* child, const glm::vec2& parentSize);

    // 设置窗口尺寸并触发布局
    void resize(const glm::vec2& windowSize, float dpiScale);

    const glm::vec2& getWindowSize() const { return m_windowSize; }

private:
    glm::vec2 m_windowSize{800, 600};
};

} // namespace geofinder
