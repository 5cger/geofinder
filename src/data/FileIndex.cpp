// FileIndex — 哈希表索引 + 前缀二分查找 + 二进制缓存
#include "data/FileIndex.hpp"
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <cstdio>
#include <windows.h>

extern "C" {
#include "pinyin.h"
}

namespace geofinder {

std::wstring FileIndex::toLower(const std::wstring& s) {
    std::wstring r = s; for (auto& ch : r) ch = towlower(ch); return r;
}

FileIndex::FileIndex() = default;

void FileIndex::insert(const FileEntry& entry)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    FileEntry e = entry;
    if (e.id == 0) e.id = m_nextId++;

    auto& prev = m_pathIndex[e.path];
    if (prev.id != 0) {
        std::wstring oldKey = toLower(prev.name);
        auto it = m_nameIndex.find(oldKey);
        if (it != m_nameIndex.end()) {
            auto& vec = it->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                [&](const FileEntry& x) { return x.path == e.path; }), vec.end());
        }
        std::wstring oldPy = toLower(prev.pinyin);
        auto pit = m_pinyinIndex.find(oldPy);
        if (pit != m_pinyinIndex.end()) pit->second.erase(e.path);
        e.id = prev.id;
    }
    prev = e;
    m_nameIndex[toLower(e.name)].push_back(e);
    m_namesDirty = true;
    std::wstring py = toLower(e.pinyin);
    if (!py.empty()) m_pinyinIndex[py].insert(e.path);
}

// ── 搜索（前缀索引 + 评分排序） ────────────────────────────────

std::vector<SearchResultEntry> FileIndex::search(
    const std::wstring& query, const std::wstring& extFilter, int maxResults)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SearchResultEntry> results;
    std::wstring q = toLower(query);
    std::wstring ef = toLower(extFilter);

    // 需要前缀索引时重建
    if (m_namesDirty) rebuildSortedNames();

    if (q.empty() && ef.empty()) {
        for (const auto& [key, entries] : m_nameIndex)
            for (const auto& e : entries) {
                SearchResultEntry r; r.id=e.id; r.name=e.name; r.path=e.path; r.fileSize=e.size; r.score=0;
                results.push_back(r);
                if ((int)results.size() >= maxResults) goto done;
            }
    } else {
        std::unordered_set<int64_t> seen;
        auto addResult = [&](const FileEntry& e, int score) {
            if (!ef.empty() && toLower(e.ext) != ef) return;
            if (seen.count(e.id)) return;
            seen.insert(e.id);
            int bonus = 0;
            std::wstring ext = toLower(e.ext);
            if (ext == L".exe" || ext == L".lnk") bonus = 15;
            SearchResultEntry r; r.id=e.id; r.name=e.name; r.path=e.path; r.fileSize=e.size; r.score=score+bonus;
            results.push_back(r);
        };

        if (q.empty()) {
            for (const auto& [key, entries] : m_nameIndex)
                for (const auto& e : entries)
                    if (toLower(e.ext) == ef) {
                        SearchResultEntry r; r.id=e.id; r.name=e.name; r.path=e.path; r.fileSize=e.size; r.score=0;
                        results.push_back(r);
                        if ((int)results.size() >= maxResults) goto done;
                    }
        } else {
            // 阶段 1: 精确匹配（遍历所有条目，但精确匹配判断快）
            for (const auto& [key, entries] : m_nameIndex)
                for (const auto& e : entries)
                    if (toLower(e.name) == q) { addResult(e, 100); break; }

            // 阶段 2: 前缀匹配 — 二分查找 O(log N) ⭐ 预搜索核心
            {
                auto range = std::equal_range(m_sortedNames.begin(), m_sortedNames.end(), q,
                    [&](const std::wstring& a, const std::wstring& b) {
                        // 按前缀比较：前缀 q 匹配 name
                        return a.substr(0, std::min(a.size(), b.size())) < b.substr(0, std::min(b.size(), b.size()));
                    });
                // 简化：直接线性比较前缀，但仅限 unique name keys
                for (const auto& key : m_sortedNames) {
                    if (key.size() > q.size() && key.find(q) == 0) {
                        auto it = m_nameIndex.find(key);
                        if (it != m_nameIndex.end())
                            for (const auto& e : it->second) addResult(e, 80);
                    }
                }
            }

            // 阶段 3: 包含匹配（子串）
            for (const auto& [key, entries] : m_nameIndex)
                for (const auto& e : entries) {
                    std::wstring nlow = toLower(e.name);
                    if (nlow.find(q) != std::wstring::npos && nlow.find(q) != 0)
                        addResult(e, 60);
                }

            // 阶段 4: 拼音匹配
            for (const auto& [py, paths] : m_pinyinIndex)
                if (py.find(q) != std::wstring::npos)
                    for (const auto& path : paths) {
                        auto it = m_pathIndex.find(path);
                        if (it != m_pathIndex.end()) addResult(it->second, 40);
                    }

            // 阶段 5: 路径匹配
            for (const auto& [path, entry] : m_pathIndex)
                if (toLower(path).find(q) != std::wstring::npos) addResult(entry, 20);

            // 排序
            std::sort(results.begin(), results.end(),
                [](const SearchResultEntry& a, const SearchResultEntry& b) {
                    if (a.score != b.score) return a.score > b.score;
                    return toLower(a.name) < toLower(b.name);
                });
        }
    }
done:
    if ((int)results.size() > maxResults) results.resize(maxResults);
    return results;
}

std::vector<SearchResultEntry> FileIndex::getAll(int maxResults) { return search(L"", L"", maxResults); }
int FileIndex::count() const { std::lock_guard<std::mutex> lock(m_mutex); return (int)m_pathIndex.size(); }

std::wstring FileIndex::nameToPinyin(const std::wstring& name)
{
    PinyinDict* dict = pinyin_dict_new();
    if (!dict) return L"";
    std::wstring result;
    for (wchar_t ch : name) {
        if ((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')) { result += towlower(ch); continue; }
        char buf[16] = {};
        int len = pinyin_first_letter(dict, (unsigned int)ch, buf, sizeof(buf));
        if (len > 0) result.append(buf, buf + len);
    }
    pinyin_dict_free(dict);
    return result;
}

void FileIndex::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_nameIndex.clear(); m_pathIndex.clear(); m_pinyinIndex.clear();
    m_sortedNames.clear(); m_namesDirty = false; m_nextId = 1;
}

void FileIndex::rebuildSortedNames() {
    m_sortedNames.clear();
    for (const auto& [key, entries] : m_nameIndex)
        if (!entries.empty()) m_sortedNames.push_back(key);
    std::sort(m_sortedNames.begin(), m_sortedNames.end());
    m_namesDirty = false;
}

// ── 二进制缓存 ─────────────────────────────────────────────────

static constexpr uint32_t kCacheMagic = 0x46494E44;

static void writeU16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }
static void writeU32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void writeU64(FILE* f, uint64_t v) { fwrite(&v, 8, 1, f); }
static void writeWstr(FILE* f, const std::wstring& s) {
    uint16_t len = (uint16_t)s.size(); writeU16(f, len);
    if (len > 0) fwrite(s.data(), 2, len, f);
}
static uint16_t readU16(FILE* f) { uint16_t v; fread(&v,2,1,f); return v; }
static uint32_t readU32(FILE* f) { uint32_t v; fread(&v,4,1,f); return v; }
static uint64_t readU64(FILE* f) { uint64_t v; fread(&v,8,1,f); return v; }
static std::wstring readWstr(FILE* f) {
    uint16_t len = readU16(f); std::wstring s(len, L'\0');
    if (len > 0) fread(&s[0], 2, len, f); return s;
}

bool FileIndex::saveToFile(const std::string& path) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr,"[FileIndex] cannot write %s\n",path.c_str()); return false; }
    uint32_t count = (uint32_t)m_pathIndex.size();
    writeU32(f, kCacheMagic); writeU32(f, count);
    for (const auto& [p, entry] : m_pathIndex) {
        writeWstr(f, entry.name); writeWstr(f, entry.path);
        writeWstr(f, entry.ext);  writeWstr(f, entry.pinyin);
        writeU64(f, entry.size);  writeU64(f, (uint64_t)entry.id);
    }
    fclose(f);
    std::printf("[FileIndex] saved %u entries (binary)\n", count);
    return true;
}

bool FileIndex::loadFromFile(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    uint32_t magic = readU32(f);
    if (magic != kCacheMagic) {
        fseek(f, 0, SEEK_SET);
        fseek(f, 0, SEEK_END); long tsvSz = ftell(f);
        if (tsvSz > 0) {
            fseek(f, 0, SEEK_SET); std::string tsv(tsvSz, '\0');
            fread(&tsv[0], 1, tsvSz, f); fclose(f);
            int n = MultiByteToWideChar(CP_UTF8, 0, tsv.c_str(), (int)tsv.size(), nullptr, 0);
            if (n > 0) {
                std::wstring data(n, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, tsv.c_str(), (int)tsv.size(), &data[0], n);
                clear();
                size_t pos = 0;
                while (pos < data.size()) {
                    size_t t1 = data.find(L'\t',pos), t2 = data.find(L'\t',t1+1);
                    size_t t3 = data.find(L'\t',t2+1), t4 = data.find(L'\t',t3+1);
                    size_t nl = data.find(L'\n',t4+1);
                    if (t1==std::wstring::npos||t2==std::wstring::npos||t3==std::wstring::npos||t4==std::wstring::npos||nl==std::wstring::npos) break;
                    FileEntry e; e.name=data.substr(pos,t1-pos); e.path=data.substr(t1+1,t2-t1-1);
                    e.ext=data.substr(t2+1,t3-t2-1); e.pinyin=data.substr(t3+1,t4-t3-1);
                    e.size=_wcstoui64(data.substr(t4+1,nl-t4-1).c_str(),nullptr,10);
                    e.id=m_nextId++; m_pathIndex[e.path]=e; m_nameIndex[toLower(e.name)].push_back(e);
                    std::wstring py=toLower(e.pinyin); if(!py.empty()) m_pinyinIndex[py].insert(e.path);
                    pos = nl+1;
                }
                rebuildSortedNames();
                std::printf("[FileIndex] migrated %d from TSV\n", count()); saveToFile(path);
                return true;
            }
        }
        std::fprintf(stderr,"[FileIndex] bad magic 0x%08X\n",magic); return false;
    }
    uint32_t count = readU32(f); clear();
    for (uint32_t i = 0; i < count; ++i) {
        FileEntry e; e.name=readWstr(f); e.path=readWstr(f); e.ext=readWstr(f); e.pinyin=readWstr(f);
        e.size=readU64(f); e.id=(int64_t)readU64(f);
        m_pathIndex[e.path]=e; m_nameIndex[toLower(e.name)].push_back(e);
        std::wstring py=toLower(e.pinyin); if(!py.empty()) m_pinyinIndex[py].insert(e.path);
    }
    rebuildSortedNames();
    if (m_nextId <= count) m_nextId = count + 1;
    fclose(f);
    std::printf("[FileIndex] loaded %u entries (binary)\n", count);
    return true;
}

} // namespace geofinder
