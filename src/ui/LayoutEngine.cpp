// 参考蓝图 §5.8 — LayoutEngine 实现
#include "ui/LayoutEngine.hpp"
#include <algorithm>

namespace geofinder {

void LayoutEngine::layoutVertical(Widget* parent,
                                   const std::vector<Widget*>& children,
                                   float padding, float spacing)
{
    float y = padding;
    for (Widget* child : children) {
        if (!child || !child->isVisible()) continue;
        float x = padding;
        float w = (parent ? parent->getSize().x : m_windowSize.x) - padding * 2.0f;
        float h = child->getSize().y > 0 ? child->getSize().y : 40.0f;
        child->layout(glm::vec2(x, y), glm::vec2(w, h));
        y += h + spacing;
    }
}

void LayoutEngine::layoutHorizontal(Widget* parent,
                                     const std::vector<Widget*>& children,
                                     float padding, float spacing)
{
    float x = padding;
    for (Widget* child : children) {
        if (!child || !child->isVisible()) continue;
        float y = padding;
        float w = child->getSize().x > 0 ? child->getSize().x : 100.0f;
        float h = (parent ? parent->getSize().y : m_windowSize.y) - padding * 2.0f;
        child->layout(glm::vec2(x, y), glm::vec2(w, h));
        x += w + spacing;
    }
}

void LayoutEngine::centerInParent(Widget* child, const glm::vec2& parentSize)
{
    if (!child) return;
    glm::vec2 cs = child->getSize();
    float x = (parentSize.x - cs.x) * 0.5f;
    float y = (parentSize.y - cs.y) * 0.5f;
    child->layout(glm::vec2(std::max(0.0f, x), std::max(0.0f, y)), cs);
}

void LayoutEngine::resize(const glm::vec2& windowSize, float dpiScale)
{
    m_windowSize = windowSize * dpiScale;
}

} // namespace geofinder
