// FileIndex — 内存哈希表文件索引器（优化版：nameLow 缓存 + O(1) 精确 + 合并遍历 + static dict）
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
    std::wstring nameLow;    // 小写文件名（缓存，搜索零分配）
    std::wstring path;       // 完整路径
    std::wstring ext;        // 扩展名（小写，含 .）
    std::wstring pinyin;     // 拼音首字母
    uint64_t size = 0;
    int64_t id = 0;
};

class FileIndex {
public:
    FileIndex();
    void insert(const FileEntry& entry);
    std::vector<SearchResultEntry> search(const std::wstring& query,
                                          const std::wstring& extFilter = L"",
                                          int maxResults = 500);
    std::vector<SearchResultEntry> getAll(int maxResults = 20);
    int count() const;
    void clear();
    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);
    bool empty() const { return count() == 0; }
    static std::wstring toLower(const std::wstring& s);
    static std::wstring nameToPinyin(const std::wstring& name);
    void loadPinyinCache(const std::string& path);
    void savePinyinCache(const std::string& path) const;

private:
    std::unordered_map<std::wstring, std::vector<FileEntry>> m_nameIndex;
    std::unordered_map<std::wstring, FileEntry> m_pathIndex;
    std::unordered_map<std::wstring, std::unordered_set<std::wstring>> m_pinyinIndex;
    mutable std::mutex m_mutex;
    int64_t m_nextId = 1;
    void rebuildSortedNames();
    std::vector<std::wstring> m_sortedNames;
    bool m_namesDirty = false;
};

} // namespace geofinder
