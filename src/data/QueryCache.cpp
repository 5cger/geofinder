// 参考蓝图 §5.20 — QueryCache 实现
//
// 简单的 HashMap 缓存，TTL + LRU-like 淘汰 + 增量失效。

#include "data/QueryCache.hpp"

#include <algorithm>
#include <cstdio>

namespace geofinder {

// ── 构造 / 析构 ─────────────────────────────────────────────────────

QueryCache::QueryCache(IndexDatabase* db, size_t maxEntries,
                       std::chrono::seconds ttl)
    : m_db(db)
    , m_maxEntries(maxEntries)
    , m_ttl(ttl)
{
}

// ── query ────────────────────────────────────────────────────────────

std::vector<SearchResultEntry> QueryCache::query(
    const std::string& queryString)
{
    // 参考蓝图 §5.20 — 先查缓存，miss 则查数据库

    // 空查询直接返回空
    if (queryString.empty()) return {};

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 清理过期条目
        evictExpired();

        // 缓存命中
        auto it = m_cache.find(queryString);
        if (it != m_cache.end()) {
            std::printf("[QueryCache] hit: \"%s\" (%zu results)\n",
                        queryString.c_str(), it->second.results.size());
            return it->second.results;
        }
    }

    // 缓存 miss → 查数据库
    if (!m_db) return {};

    auto results = m_db->search(queryString);

    // 写入缓存
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 淘汰旧条目（如果超过上限）
        if (m_cache.size() >= m_maxEntries) {
            evictOldest();
        }

        CacheEntry entry;
        entry.results = results;
        entry.createdAt = std::chrono::steady_clock::now();
        // 收集涉及的 appId 集合（用于增量失效）
        for (const auto& r : results) {
            entry.appIds.insert(r.id);
        }

        m_cache[queryString] = std::move(entry);
    }

    std::printf("[QueryCache] miss: \"%s\" → db returned %zu results\n",
                queryString.c_str(), results.size());
    return results;
}

// ── invalidateApp ────────────────────────────────────────────────────

void QueryCache::invalidateApp(int64_t appId)
{
    // 参考蓝图 §5.20 — 遍历缓存，移除包含此 appId 的条目
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_cache.begin();
    while (it != m_cache.end()) {
        if (it->second.appIds.count(appId) > 0) {
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}

// ── invalidateAll ────────────────────────────────────────────────────

void QueryCache::invalidateAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();
}

// ── size ─────────────────────────────────────────────────────────────

size_t QueryCache::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cache.size();
}

// ── evictExpired ─────────────────────────────────────────────────────

void QueryCache::evictExpired()
{
    auto now = std::chrono::steady_clock::now();

    auto it = m_cache.begin();
    while (it != m_cache.end()) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second.createdAt);
        if (age >= m_ttl) {
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}

// ── evictOldest ──────────────────────────────────────────────────────

void QueryCache::evictOldest()
{
    if (m_cache.empty()) return;

    // 找到最旧的条目并删除
    auto oldest = m_cache.begin();
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        if (it->second.createdAt < oldest->second.createdAt) {
            oldest = it;
        }
    }
    m_cache.erase(oldest);
}

} // namespace geofinder
