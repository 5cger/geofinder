// 参考蓝图 §5.20 — QueryCache 查询结果缓存
//
// HashMap 缓存：key=查询字符串，value=搜索结果 + 过期时间。
// TTL 默认 300 秒，支持增量失效（按 appId）和全量清空。
#pragma once

#include "data/IndexDatabase.hpp"

#include <chrono>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace geofinder {

class QueryCache {
public:
    /// @param db          底层数据库（缓存 miss 时回源查询）
    /// @param maxEntries  最大缓存条目数（默认 200）
    /// @param ttl         缓存有效期（默认 300 秒）
    QueryCache(IndexDatabase* db, size_t maxEntries = 200,
               std::chrono::seconds ttl = std::chrono::seconds(300));

    /// 查询：先查缓存（命中且未过期直接返回），miss 则查数据库并写入缓存。
    std::vector<SearchResultEntry> query(const std::string& queryString);

    /// 增量失效：清除包含指定 appId 的缓存条目。
    /// 适用于单个 .lnk 增删改。
    void invalidateApp(int64_t appId);

    /// 全量清空所有缓存。
    /// 适用于批量扫描完成或大量变更。
    void invalidateAll();

    /// 返回当前缓存条目数（调试用）
    size_t size() const;

private:
    IndexDatabase* m_db;
    size_t m_maxEntries;
    std::chrono::seconds m_ttl;

    struct CacheEntry {
        std::vector<SearchResultEntry> results;
        std::chrono::steady_clock::time_point createdAt;
        std::set<int64_t> appIds;  // 此结果涉及的 appId（用于增量失效）
    };

    std::unordered_map<std::string, CacheEntry> m_cache;
    mutable std::mutex m_mutex;

    /// 清理所有过期条目
    void evictExpired();

    /// 清理最旧的条目直到低于 maxEntries
    void evictOldest();
};

} // namespace geofinder
