// 参考蓝图 §5.9 — AnimationEngine 动画引擎
//
// 轻量补间动画系统。支持 float 属性的线性/缓动插值。
// 通过 TargetSetter 回调每帧更新目标值。
//
// 使用示例（窗口缩放淡入 200ms ease-out）：
//   m_anim.addTween(0.0f, 1.0f, 0.2f,
//       [this](float v) {
//           m_windowScale = v;
//           m_windowOpacity = v;
//           m_uiMgr->markDirty();
//       }, Easing::EaseOut);
#pragma once

#include <glm/glm.hpp>
#include <functional>
#include <vector>
#include <cstdint>

namespace geofinder {

enum class Easing { Linear, EaseOut, EaseIn, EaseInOut };

class AnimationEngine {
public:
    using TargetSetter = std::function<void(float)>;

    // 添加一个属性动画
    // duration 单位秒。setter 每帧被调用，参数为插值后的值。
    uint64_t addTween(float from, float to, float duration,
                      TargetSetter setter, Easing easing = Easing::EaseOut);

    // 添加延迟回调
    uint64_t addDelay(float delay, std::function<void()> callback);

    // 取消动画
    void cancel(uint64_t id);

    // 每帧更新
    void update(float delta);

    bool hasActive() const { return !m_tweens.empty() || !m_delayed.empty(); }

private:
    struct Tween {
        uint64_t id;
        float from;
        float to;
        float duration;
        float elapsed = 0;
        TargetSetter setter;
        Easing easing;
    };
    std::vector<Tween> m_tweens;

    struct DelayedCallback {
        uint64_t id;
        float remaining;
        std::function<void()> callback;
    };
    std::vector<DelayedCallback> m_delayed;

    uint64_t m_nextId = 1;

    static float applyEasing(Easing e, float t);
};

} // namespace geofinder
