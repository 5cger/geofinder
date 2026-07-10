// 参考蓝图 §5.9 — AnimationEngine 实现
#include "ui/AnimationEngine.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace geofinder {

// ── 缓动函数 ──────────────────────────────────────────────────────

float AnimationEngine::applyEasing(Easing e, float t)
{
    // t ∈ [0, 1]，返回插值因子
    switch (e) {
    case Easing::Linear:
        return t;
    case Easing::EaseOut:
        // 二次缓出：1 - (1-t)^2
        return 1.0f - (1.0f - t) * (1.0f - t);
    case Easing::EaseIn:
        // 二次缓入：t^2
        return t * t;
    case Easing::EaseInOut:
        // 二次缓入缓出
        return (t < 0.5f) ? (2.0f * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f);
    }
    return t;
}

// ── 添加补间 ──────────────────────────────────────────────────────

uint64_t AnimationEngine::addTween(float from, float to, float duration,
                                    TargetSetter setter, Easing easing)
{
    uint64_t id = m_nextId++;
    m_tweens.push_back({id, from, to, duration, 0.0f, std::move(setter), easing});
    return id;
}

// ── 添加延迟回调 ─────────────────────────────────────────────────

uint64_t AnimationEngine::addDelay(float delay, std::function<void()> callback)
{
    uint64_t id = m_nextId++;
    m_delayed.push_back({id, delay, std::move(callback)});
    return id;
}

// ── 取消动画 ──────────────────────────────────────────────────────

void AnimationEngine::cancel(uint64_t id)
{
    auto& tweens = m_tweens;
    tweens.erase(std::remove_if(tweens.begin(), tweens.end(),
        [id](const Tween& t) { return t.id == id; }),
        tweens.end());

    auto& delayed = m_delayed;
    delayed.erase(std::remove_if(delayed.begin(), delayed.end(),
        [id](const DelayedCallback& d) { return d.id == id; }),
        delayed.end());
}

// ── 每帧更新 ──────────────────────────────────────────────────────

void AnimationEngine::update(float delta)
{
    // 更新补间
    for (auto it = m_tweens.begin(); it != m_tweens.end(); ) {
        it->elapsed += delta;
        float t = std::min(it->elapsed / it->duration, 1.0f);
        float factor = applyEasing(it->easing, t);
        float value = it->from + (it->to - it->from) * factor;
        if (it->setter) {
            it->setter(value);
        }
        if (t >= 1.0f) {
            it = m_tweens.erase(it);
        } else {
            ++it;
        }
    }

    // 更新延迟回调
    for (auto it = m_delayed.begin(); it != m_delayed.end(); ) {
        it->remaining -= delta;
        if (it->remaining <= 0.0f) {
            if (it->callback) {
                it->callback();
            }
            it = m_delayed.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace geofinder
