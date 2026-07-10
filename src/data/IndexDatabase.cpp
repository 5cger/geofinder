// 参考蓝图 §5.17 — IndexDatabase 实现
//
// SQLite3 + FTS5 全文搜索。双连接架构（主线程只读 / 后台写）。
// BM25 列权重：name=10, path=1, source=1, alias=5, pinyin=5

#include "data/IndexDatabase.hpp"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <algorithm>

#include <windows.h>

#include "utils/StringUtils.hpp"

namespace geofinder {

// ── 构造 / 析构 ─────────────────────────────────────────────────────

IndexDatabase::IndexDatabase(const std::string& dbPath)
    : m_dbPath(dbPath) {}

IndexDatabase::~IndexDatabase() {
    if (m_readDb) {
        sqlite3_close(m_readDb);
        m_readDb = nullptr;
    }
    if (m_writeDb) {
        sqlite3_close(m_writeDb);
        m_writeDb = nullptr;
    }
}

// ── init ─────────────────────────────────────────────────────────────

bool IndexDatabase::init() {
    // 打开只读连接（主线程）
    int rc = sqlite3_open_v2(m_dbPath.c_str(), &m_readDb,
        SQLITE_OPEN_READONLY | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        std::fprintf(stderr, "[IndexDatabase] open read db failed: %s\n",
                     sqlite3_errmsg(m_readDb));
        return false;
    }

    // 打开写连接（后台线程）
    rc = sqlite3_open_v2(m_dbPath.c_str(), &m_writeDb,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        std::fprintf(stderr, "[IndexDatabase] open write db failed: %s\n",
                     sqlite3_errmsg(m_writeDb));
        return false;
    }

    // 启用 WAL 模式（读写并发友好）
    {
        char* err = nullptr;
        rc = sqlite3_exec(m_writeDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::fprintf(stderr, "[IndexDatabase] WAL mode failed: %s\n", err);
            sqlite3_free(err);
            return false;
        }
    }

    if (!createTables()) return false;
    if (!createFts()) return false;
    if (!createTriggers()) return false;

    std::printf("[IndexDatabase] init OK: %s\n", m_dbPath.c_str());
    return true;
}

// ── createTables ─────────────────────────────────────────────────────

bool IndexDatabase::createTables() {
    const char* sql = R"SQL(
        CREATE TABLE IF NOT EXISTS apps (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            name            TEXT NOT NULL,
            path            TEXT NOT NULL,
            arguments       TEXT DEFAULT '',
            working_dir     TEXT DEFAULT '',
            alias           TEXT DEFAULT '',
            pinyin          TEXT DEFAULT '',
            icon_cache_key  TEXT DEFAULT '',
            app_user_model_id TEXT DEFAULT '',
            is_uwp          INTEGER DEFAULT 0,
            launch_count    INTEGER DEFAULT 0,
            last_launched   INTEGER DEFAULT 0
        );
    )SQL";

    char* err = nullptr;
    int rc = sqlite3_exec(m_writeDb, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::fprintf(stderr, "[IndexDatabase] create tables: %s\n", err);
        sqlite3_free(err);
        return false;
    }
    return true;
}

// ── createFts ────────────────────────────────────────────────────────

bool IndexDatabase::createFts() {
    const char* sql = R"SQL(
        CREATE VIRTUAL TABLE IF NOT EXISTS apps_fts USING fts5(
            name, path, source, alias, pinyin,
            content='apps',
            content_rowid='id'
        );
    )SQL";

    char* err = nullptr;
    int rc = sqlite3_exec(m_writeDb, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::fprintf(stderr, "[IndexDatabase] create FTS: %s\n", err);
        sqlite3_free(err);
        return false;
    }
    return true;
}

// ── createTriggers ───────────────────────────────────────────────────

bool IndexDatabase::createTriggers() {
    // 蓝图 §5.17 三个触发器：INSERT / DELETE / UPDATE 自动同步 FTS
    const char* sqls[] = {
        R"SQL(
            CREATE TRIGGER IF NOT EXISTS apps_ai AFTER INSERT ON apps BEGIN
                INSERT INTO apps_fts(rowid, name, path, source, alias, pinyin)
                VALUES (new.id, new.name, new.path, new.path, new.alias, new.pinyin);
            END;
        )SQL",
        R"SQL(
            CREATE TRIGGER IF NOT EXISTS apps_ad AFTER DELETE ON apps BEGIN
                INSERT INTO apps_fts(apps_fts, rowid, name, path, source, alias, pinyin)
                VALUES ('delete', old.id, old.name, old.path, old.path, old.alias, old.pinyin);
            END;
        )SQL",
        R"SQL(
            CREATE TRIGGER IF NOT EXISTS apps_au AFTER UPDATE ON apps BEGIN
                INSERT INTO apps_fts(apps_fts, rowid, name, path, source, alias, pinyin)
                VALUES ('delete', old.id, old.name, old.path, old.path, old.alias, old.pinyin);
                INSERT INTO apps_fts(rowid, name, path, source, alias, pinyin)
                VALUES (new.id, new.name, new.path, new.path, new.alias, new.pinyin);
            END;
        )SQL"
    };

    for (const char* sql : sqls) {
        char* err = nullptr;
        int rc = sqlite3_exec(m_writeDb, sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::fprintf(stderr, "[IndexDatabase] create trigger: %s\n", err);
            sqlite3_free(err);
            return false;
        }
    }
    return true;
}

// ── search ───────────────────────────────────────────────────────────

std::vector<SearchResultEntry> IndexDatabase::search(const std::string& query,
                                                      int limit) {
    // 参考蓝图 §5.17 — FTS5 + BM25 搜索
    //
    // bm25(10,1,1,5,5): name=10, path=1, source=1, alias=5, pinyin=5
    // 返回升序（分数越低越匹配），C++ 层再重排取前 20

    std::vector<SearchResultEntry> results;

    if (!m_readDb || query.empty()) return results;

    // 构建 FTS5 查询：转义特殊字符，追加通配符 *
    std::string ftsQuery;
    ftsQuery.reserve(query.size() * 2 + 4);
    for (char ch : query) {
        // FTS5 特殊字符需要引号包裹
        if (ch == '"' || ch == '*' || ch == '(' || ch == ')' ||
            ch == '^' || ch == '\\') {
            ftsQuery += '"';
            ftsQuery += ch;
            ftsQuery += '"';
        } else {
            ftsQuery += ch;
        }
    }
    ftsQuery += '*';  // 前缀匹配

    const char* sql = R"SQL(
        SELECT a.id, a.name, a.path, a.icon_cache_key,
               a.app_user_model_id, a.is_uwp,
               a.launch_count, a.last_launched,
               bm25(apps_fts, 10.0, 1.0, 1.0, 5.0, 5.0) AS score
        FROM apps_fts f
        JOIN apps a ON a.id = f.rowid
        WHERE apps_fts MATCH ?
        ORDER BY score ASC
        LIMIT ?
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_readDb, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::fprintf(stderr, "[IndexDatabase] search prepare: %s\n",
                     sqlite3_errmsg(m_readDb));
        return results;
    }

    sqlite3_bind_text(stmt, 1, ftsQuery.c_str(), (int)ftsQuery.size(),
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SearchResultEntry entry;
        entry.id          = sqlite3_column_int64(stmt, 0);
        entry.name        = StringUtils::utf8ToWide(
            (const char*)sqlite3_column_text(stmt, 1));
        entry.path        = StringUtils::utf8ToWide(
            (const char*)sqlite3_column_text(stmt, 2));
        entry.iconCacheKey = StringUtils::utf8ToWide(
            (const char*)sqlite3_column_text(stmt, 3));
        entry.appUserModelId = StringUtils::utf8ToWide(
            (const char*)sqlite3_column_text(stmt, 4));
        entry.isUWP       = sqlite3_column_int(stmt, 5) != 0;

        // launch_count / last_launched 用于 C++ 层 computeFinalScore
        int launchCount    = sqlite3_column_int(stmt, 6);
        (void)sqlite3_column_int64(stmt, 7);  // last_launched (TODO:Sprint5)
        double bm25Score   = sqlite3_column_double(stmt, 8);

        // 综合排序（蓝图 §5.17）：
        // 归一化 BM25（越低越好 → 反转），加权启动次数和新近度
        double bm25Norm = 1.0 / (1.0 + bm25Score);  // [0,1]，越高越好
        double freqWeight = std::min(static_cast<double>(launchCount) / 100.0, 1.0);
        double recencyWeight = 1.0;  // TODO(Sprint5): 基于 lastLaunch 计算新近度
        entry.score = bm25Norm * 5.0 + freqWeight * 3.0 + recencyWeight * 2.0;

        results.push_back(std::move(entry));
    }

    sqlite3_finalize(stmt);

    // 按综合分数降序排列，取前 20
    std::sort(results.begin(), results.end(),
              [](const SearchResultEntry& a, const SearchResultEntry& b) {
                  return a.score > b.score;
              });

    if ((int)results.size() > 20) {
        results.resize(20);
    }

    return results;
}

// ── getRecentApps ─────────────────────────────────────────────────

std::vector<SearchResultEntry> IndexDatabase::getRecentApps(int limit)
{
    // 无查询时按启动频次降序展示已索引应用
    std::vector<SearchResultEntry> results;
    if (!m_readDb) return results;

    const char* sql =
        "SELECT id, name, path, icon_cache_key, "
        "       app_user_model_id, is_uwp, "
        "       launch_count, last_launched "
        "FROM apps ORDER BY launch_count DESC LIMIT ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_readDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SearchResultEntry entry;
        entry.id          = sqlite3_column_int64(stmt, 0);
        entry.name        = StringUtils::utf8ToWide(
            (const char*)sqlite3_column_text(stmt, 1));
        entry.path        = StringUtils::utf8ToWide(
            (const char*)sqlite3_column_text(stmt, 2));
        entry.iconCacheKey = StringUtils::utf8ToWide(
            (const char*)sqlite3_column_text(stmt, 3));
        entry.appUserModelId = StringUtils::utf8ToWide(
            (const char*)sqlite3_column_text(stmt, 4));
        entry.isUWP       = sqlite3_column_int(stmt, 5) != 0;
        entry.score       = static_cast<double>(sqlite3_column_int(stmt, 6));
        results.push_back(std::move(entry));
    }

    sqlite3_finalize(stmt);
    return results;
}

// ── getAppCount ───────────────────────────────────────────────────

int IndexDatabase::getAppCount()
{
    if (!m_readDb) return 0;

    const char* sql = "SELECT COUNT(*) FROM apps";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_readDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

// ── upsertApp ────────────────────────────────────────────────────────

int64_t IndexDatabase::upsertApp(const AppRecord& record) {
    std::lock_guard<std::mutex> lock(m_writeMutex);

    // 按 path 查找已有记录（避免重复）
    std::string pathUtf8 = StringUtils::wideToUtf8(record.path);
    const char* findSql = "SELECT id FROM apps WHERE path = ? LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    int64_t existingId = 0;

    if (sqlite3_prepare_v2(m_writeDb, findSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, pathUtf8.c_str(), (int)pathUtf8.size(),
                          SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            existingId = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    if (existingId > 0) {
        // UPDATE
        const char* updSql = R"SQL(
            UPDATE apps SET name=?, arguments=?, working_dir=?, alias=?,
                   pinyin=?, icon_cache_key=?, app_user_model_id=?, is_uwp=?
            WHERE id = ?
        )SQL";
        if (sqlite3_prepare_v2(m_writeDb, updSql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string nameUtf8    = StringUtils::wideToUtf8(record.name);
            std::string argsUtf8    = StringUtils::wideToUtf8(record.arguments);
            std::string dirUtf8     = StringUtils::wideToUtf8(record.workingDir);
            std::string aliasUtf8   = StringUtils::wideToUtf8(record.alias);
            std::string pinyinUtf8  = StringUtils::wideToUtf8(record.pinyin);
            std::string iconUtf8    = StringUtils::wideToUtf8(record.iconCacheKey);
            std::string umidUtf8    = StringUtils::wideToUtf8(record.appUserModelId);

            sqlite3_bind_text(stmt, 1, nameUtf8.c_str(), (int)nameUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, argsUtf8.c_str(), (int)argsUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, dirUtf8.c_str(), (int)dirUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, aliasUtf8.c_str(), (int)aliasUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5, pinyinUtf8.c_str(), (int)pinyinUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, iconUtf8.c_str(), (int)iconUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 7, umidUtf8.c_str(), (int)umidUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 8, record.isUWP ? 1 : 0);
            sqlite3_bind_int64(stmt, 9, existingId);

            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        return existingId;
    } else {
        // INSERT
        const char* insSql = R"SQL(
            INSERT INTO apps (name, path, arguments, working_dir, alias,
                             pinyin, icon_cache_key, app_user_model_id, is_uwp,
                             launch_count, last_launched)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        )SQL";
        if (sqlite3_prepare_v2(m_writeDb, insSql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string nameUtf8   = StringUtils::wideToUtf8(record.name);
            std::string argsUtf8   = StringUtils::wideToUtf8(record.arguments);
            std::string dirUtf8    = StringUtils::wideToUtf8(record.workingDir);
            std::string aliasUtf8  = StringUtils::wideToUtf8(record.alias);
            std::string pinyinUtf8 = StringUtils::wideToUtf8(record.pinyin);
            std::string iconUtf8   = StringUtils::wideToUtf8(record.iconCacheKey);
            std::string umidUtf8   = StringUtils::wideToUtf8(record.appUserModelId);

            sqlite3_bind_text(stmt, 1, nameUtf8.c_str(), (int)nameUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, pathUtf8.c_str(), (int)pathUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, argsUtf8.c_str(), (int)argsUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, dirUtf8.c_str(), (int)dirUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5, aliasUtf8.c_str(), (int)aliasUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, pinyinUtf8.c_str(), (int)pinyinUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 7, iconUtf8.c_str(), (int)iconUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 8, umidUtf8.c_str(), (int)umidUtf8.size(), SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 9, record.isUWP ? 1 : 0);
            sqlite3_bind_int(stmt, 10, record.launchCount);
            sqlite3_bind_int64(stmt, 11, record.lastLaunched);

            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        return sqlite3_last_insert_rowid(m_writeDb);
    }
}

// ── deleteApp ────────────────────────────────────────────────────────

void IndexDatabase::deleteApp(int64_t id) {
    std::lock_guard<std::mutex> lock(m_writeMutex);

    const char* sql = "DELETE FROM apps WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_writeDb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

// ── updateLaunchStats ────────────────────────────────────────────────

void IndexDatabase::updateLaunchStats(int64_t appId) {
    std::lock_guard<std::mutex> lock(m_writeMutex);

    const char* sql =
        "UPDATE apps SET launch_count = launch_count + 1, "
        "last_launched = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_writeDb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1,
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        sqlite3_bind_int64(stmt, 2, appId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

// ── getLaunchStats ───────────────────────────────────────────────────

bool IndexDatabase::getLaunchStats(int64_t appId, int& count,
                                    int64_t& lastLaunched) {
    if (!m_readDb) return false;

    const char* sql = "SELECT launch_count, last_launched FROM apps WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_readDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int64(stmt, 1, appId);
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
        lastLaunched = sqlite3_column_int64(stmt, 1);
        ok = true;
    }
    sqlite3_finalize(stmt);
    return ok;
}

// ── integrityCheck ───────────────────────────────────────────────────

bool IndexDatabase::integrityCheck() {
    if (!m_readDb) return false;

    const char* sql = "PRAGMA integrity_check;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_readDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* result = (const char*)sqlite3_column_text(stmt, 0);
        ok = (result && strcmp(result, "ok") == 0);
    }
    sqlite3_finalize(stmt);
    return ok;
}

} // namespace geofinder
