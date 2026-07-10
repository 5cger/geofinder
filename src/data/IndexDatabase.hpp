// 参考蓝图 §5.17 — IndexDatabase 数据库
//
// SQLite3 + FTS5 全文搜索，双连接（只读主线程 / 写后台线程）。
// BM25 排序取前 100 条，C++ 层再按启动频次+时间戳重排取前 20。
#pragma once

#include "data/DataTypes.hpp"

#include <sqlite3.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace geofinder {

// ── 数据库行记录（蓝图 §5.17） ──────────────────────────────────

struct AppRecord {
    int64_t id = 0;
    std::wstring name;
    std::wstring path;
    std::wstring arguments;
    std::wstring workingDir;
    std::wstring alias;
    std::wstring pinyin;           // 拼音首字母串（逗号分隔多音字）
    std::wstring iconCacheKey;
    std::wstring appUserModelId;   // UWP 应用标识
    int launchCount = 0;
    int64_t lastLaunched = 0;
    bool isUWP = false;
};

// ── IndexDatabase（蓝图 §5.17） ─────────────────────────────────

class IndexDatabase {
public:
    explicit IndexDatabase(const std::string& dbPath);
    ~IndexDatabase();

    // 初始化数据库（创建表、FTS、触发器）
    bool init();

    // ── 搜索（主线程，只读连接） ──────────────────────────────
    //
    // 先用 FTS5 + BM25 取前 100 条，C++ 层再用 computeFinalScore
    // 重排取前 20。limit 参数控制 BM25 阶段的返回数量。
    std::vector<SearchResultEntry> search(const std::string& query,
                                          int limit = 100);

    // 无查询时获取最近/最常用的应用（按 launch_count DESC）
    std::vector<SearchResultEntry> getRecentApps(int limit = 20);

    // 获取已索引应用总数
    int getAppCount();

    // ── 启动统计更新（后台写线程） ───────────────────────────
    void updateLaunchStats(int64_t appId);

    // ── CRUD（后台写线程） ────────────────────────────────────
    int64_t upsertApp(const AppRecord& record);
    void deleteApp(int64_t id);

    // 获取启动频次和时间戳（用于 C++ 重排 score）
    bool getLaunchStats(int64_t appId, int& count,
                        int64_t& lastLaunched);

    // ── 完整性检查 ────────────────────────────────────────────
    bool integrityCheck();

private:
    sqlite3* m_readDb = nullptr;    // 主线程只读连接
    sqlite3* m_writeDb = nullptr;   // 后台写连接
    std::string m_dbPath;
    std::mutex m_writeMutex;

    bool createTables();
    bool createFts();
    bool createTriggers();
};

} // namespace geofinder
