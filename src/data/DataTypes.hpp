// 参考蓝图 §5.17 — 跨模块数据类型
//
// SearchResultEntry 被 UI 层（ResultList, UIManager）和数据层（IndexDatabase）
// 共同引用，放在独立头文件中避免循环依赖。
#pragma once

#include <string>
#include <cstdint>

namespace geofinder {

struct SearchResultEntry {
    int64_t id;
    std::wstring name;
    std::wstring path;
    std::wstring iconCacheKey;
    std::wstring appUserModelId;
    uint64_t fileSize = 0;       // 文件大小（字节）
    bool isUWP = false;
    double score = 0.0;
};

} // namespace geofinder
