// FileIndex — 内存哈希表文件索引器（增强版：拼音索引 + 评分排序）
//
// 三级索引：nameIndex(name→entries) | pathIndex(path→entry) | pinyinIndex(pinyin→path list)
// 多级评分：精确名(100) > 开头匹配(80) > 包含(60) > 拼音(40) > 路径(20)
#pragma once

#include "data/DataTypes.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <cstdint>

namespace geofinder {

struct FileEntry {
    std::wstring name;       // 文件名（含扩展名）
    std::wstring path;       // 完整路径
    std::wstring ext;        // 扩展名（小写，含 .）
    std::wstring pinyin;     // 拼音首字母（如 "wyyyy"）
    uint64_t size = 0;       // 文件大小（字节）
    int64_t id = 0;          // 自增 ID（唯一）
};

class FileIndex {
public:
    FileIndex();

    /// 添加/更新文件条目（线程安全）
    void insert(const FileEntry& entry);

    /// 按关键词搜索（大小写不敏感 + 拼音匹配 + 评分排序）
    /// @param query     搜索关键词
    /// @param extFilter 扩展名过滤（空=不过滤，如 ".pdf"）
    /// @param maxResults 最大返回数
    std::vector<SearchResultEntry> search(const std::wstring& query,
                                          const std::wstring& extFilter = L"",
                                          int maxResults = 500);

    /// 获取全部条目（空查询时展示）
    std::vector<SearchResultEntry> getAll(int maxResults = 20);

    /// 索引总数
    int count() const;

    /// 清空索引
    void clear();

    /// 持久化：保存到文件（TSV 格式）
    bool saveToFile(const std::string& path) const;

    /// 持久化：从文件加载
    bool loadFromFile(const std::string& path);

    /// 检查是否为空
    bool empty() const { return count() == 0; }

    /// 将文件名转为拼音首字母（如 "网易云音乐" → "wyyyy"）
    static std::wstring nameToPinyin(const std::wstring& name);

private:
    // 主索引：小写文件名 → 文件列表
    std::unordered_map<std::wstring, std::vector<FileEntry>> m_nameIndex;
    // 路径 → entry（唯一映射，用于去重和更新）
    std::unordered_map<std::wstring, FileEntry> m_pathIndex;
    // 拼音首字母 → 路径集合（前缀快速查找）
    std::unordered_map<std::wstring, std::unordered_set<std::wstring>> m_pinyinIndex;

    mutable std::mutex m_mutex;
    int64_t m_nextId = 1;

    static std::wstring toLower(const std::wstring& s);
    void rebuildSortedNames();

    // 前缀索引：排序后的小写名称 → 二分查找 O(log n) 而非 O(n)
    std::vector<std::wstring> m_sortedNames;
    bool m_namesDirty = false;
};

} // namespace geofinder
