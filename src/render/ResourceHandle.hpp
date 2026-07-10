#pragma once

#include <cstdint>

// 参考蓝图 §5.1 — 资源句柄类型
//
// ResourceHandle 是一个不透明 uint64_t 句柄，由 ResourceManager 分配，
// 用于跨模块引用纹理资源。句柄值 0 表示"无效句柄"。

namespace geofinder {

using ResourceHandle = uint64_t;

constexpr ResourceHandle kInvalidHandle = 0;

} // namespace geofinder
