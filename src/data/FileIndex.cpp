// FileIndex — 优化版：nameLow + O(1)精确 + 合并遍历 + static dict
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

void FileIndex::insert(const FileEntry& entry) {
    std::lock_guard<std::mutex> lock(m_mutex);
    FileEntry e = entry;
    if (e.id == 0) e.id = m_nextId++;
    if (e.nameLow.empty()) e.nameLow = toLower(e.name);

    auto& prev = m_pathIndex[e.path];
    if (prev.id != 0) {
        auto it = m_nameIndex.find(prev.nameLow);
        if (it != m_nameIndex.end())
            it->second.erase(std::remove_if(it->second.begin(), it->second.end(),
                [&](const FileEntry& x) { return x.path == e.path; }), it->second.end());
        auto pit = m_pinyinIndex.find(prev.pinyin);
        if (pit != m_pinyinIndex.end()) pit->second.erase(e.path);
        e.id = prev.id;
    }
    prev = e;
    m_nameIndex[e.nameLow].push_back(e); m_namesDirty = true;
    if (!e.pinyin.empty()) {
        std::wstring py = toLower(e.pinyin);
        if (!py.empty()) m_pinyinIndex[py].insert(e.path);
    }
}

std::vector<SearchResultEntry> FileIndex::search(
    const std::wstring& query, const std::wstring& extFilter, int maxResults)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SearchResultEntry> results;
    std::wstring q = toLower(query), ef = toLower(extFilter);
    if (m_namesDirty) rebuildSortedNames();

    if (q.empty() && ef.empty()) {
        for (const auto& [key, entries] : m_nameIndex)
            for (const auto& e : entries) {
                SearchResultEntry r; r.id=e.id;r.name=e.name;r.path=e.path;r.fileSize=e.size;r.score=0;
                results.push_back(r);
                if ((int)results.size() >= maxResults) goto done;
            }
        goto done;
    }

    if (q.empty()) {
        for (const auto& [key, entries] : m_nameIndex)
            for (const auto& e : entries)
                if (e.ext == ef) {
                    SearchResultEntry r; r.id=e.id;r.name=e.name;r.path=e.path;r.fileSize=e.size;r.score=0;
                    results.push_back(r);
                    if ((int)results.size() >= maxResults) goto done;
                }
        goto done;
    }

    {
        std::unordered_set<int64_t> seen;
    auto add = [&](const FileEntry& e, int score) {
        if (!ef.empty() && e.ext != ef) return;
        if (!seen.insert(e.id).second) return;
        int bonus = (e.ext == L".exe" || e.ext == L".lnk") ? 15 : 0;
        SearchResultEntry r; r.id=e.id;r.name=e.name;r.path=e.path;r.fileSize=e.size;r.score=score+bonus;
        results.push_back(r);
    };

    // 阶段 1: O(1) 精确匹配
    auto exact = m_nameIndex.find(q);
    if (exact != m_nameIndex.end())
        for (const auto& e : exact->second) add(e, 100);

    // 阶段 2: O(log N) 前缀
    auto lb = std::lower_bound(m_sortedNames.begin(), m_sortedNames.end(), q);
    for (auto it = lb; it != m_sortedNames.end() && it->substr(0, q.size()) == q; ++it) {
        auto ni = m_nameIndex.find(*it);
        if (ni != m_nameIndex.end())
            for (const auto& e : ni->second) add(e, 80);
    }

    // 阶段 3+4+5 合并：单次遍历 pathIndex
    for (const auto& [path, entry] : m_pathIndex) {
        if (!ef.empty() && entry.ext != ef) continue;
        if (!entry.nameLow.empty() && entry.nameLow.find(q) != std::wstring::npos && entry.nameLow != q)
            add(entry, 60);
        if (!entry.pinyin.empty()) {
            std::wstring pc = toLower(entry.pinyin);
            size_t d = pc.rfind(L'.'); if (d != std::wstring::npos) pc = pc.substr(0, d);
            int ps = 0;
            if (pc == q) ps = 85; else if (pc.find(q) == 0) ps = 70; else if (pc.find(q) != std::wstring::npos) ps = 40;
            if (ps > 0) add(entry, ps);
        }
        std::wstring pl = toLower(path);
        if (pl.find(q) != std::wstring::npos) add(entry, 20);
    }

    }
    std::sort(results.begin(), results.end(),
        [](const SearchResultEntry& a, const SearchResultEntry& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.name < b.name;
        });

done:
    if ((int)results.size() > maxResults) results.resize(maxResults);
    return results;
}

std::vector<SearchResultEntry> FileIndex::getAll(int maxResults) { return search(L"", L"", maxResults); }
int FileIndex::count() const { std::lock_guard<std::mutex> lock(m_mutex); return (int)m_pathIndex.size(); }

// ── 拼音（static dict 一次创建） ─────────────────────────────
static std::unordered_map<wchar_t, wchar_t>& pinyinRuntimeCache() {
    static std::unordered_map<wchar_t, wchar_t> c; return c;
}

std::wstring FileIndex::nameToPinyin(const std::wstring& name)
{
    static const struct { wchar_t cp; wchar_t py; } kMap[] = {
        {0x5FAE,L'w'},{0x4FE1,L'x'},{0x7F51,L'w'},{0x6613,L'y'},{0x4E91,L'y'},{0x97F3,L'y'},{0x4E50,L'y'},
        {0x5609,L'j'},{0x7ACB,L'l'},{0x521B,L'c'},{0x8BBE,L's'},{0x7F6E,L'z'},{0x626B,L's'},{0x63CF,L'm'},
        {0x76EE,L'm'},{0x5F55,L'l'},{0x72B6,L'z'},{0x6001,L't'},{0x52A8,L'd'},{0x753B,L'h'},{0x901F,L's'},
        {0x5EA6,L'd'},{0x9AD8,L'g'},{0x4EAE,L'l'},{0x5149,L'g'},{0x6807,L'b'},{0x8FD4,L'f'},{0x56DE,L'h'},
        {0x9009,L'x'},{0x62E9,L'z'},{0x8FDB,L'j'},{0x5165,L'r'},{0x6587,L'w'},{0x4EF6,L'j'},{0x8DEF,L'l'},
        {0x5F84,L'j'},{0x5B58,L'c'},{0x50A8,L'c'},{0x7F16,L'b'},{0x8F91,L'j'},{0x5220,L's'},{0x9664,L'c'},
        {0x65B0,L'x'},{0x5EFA,L'j'},{0x4FEE,L'x'},{0x6539,L'g'},{0x91CD,L'z'},{0x4E2D,L'z'},{0x56FD,L'g'},
        {0x5927,L'd'},{0x5C0F,L'x'},{0x591A,L'd'},{0x5C11,L's'},{0x4E0A,L's'},{0x4E0B,L'x'},{0x5DE6,L'z'},
        {0x53F3,L'y'},{0x786E,L'q'},{0x8BA4,L'r'},{0x53D6,L'q'},{0x6253,L'd'},{0x5F00,L'k'},{0x5173,L'g'},
        {0x5176,L'q'},{0x4ED6,L't'},{0x4EEC,L'm'},{0x6211,L'w'},{0x4F60,L'n'},{0x597D,L'h'},{0x5427,L'b'},
        {0x5417,L'm'},{0x4E0D,L'b'},{0x662F,L's'},{0x7684,L'd'},{0x4E86,L'l'},{0x4E48,L'm'},{0x4E2A,L'g'},
        {0x548C,L'h'},{0x5C31,L'j'},{0x4E5F,L'y'},{0x8FD8,L'h'},{0x8981,L'y'},{0x6709,L'y'},{0x6CA1,L'm'},
        {0x4EBA,L'r'},{0x5728,L'z'},{0x5230,L'd'},{0x53BB,L'q'},{0x6765,L'l'},{0x8BF4,L's'},{0x770B,L'k'},
        {0x542C,L't'},{0x5199,L'x'},{0x8BFB,L'd'},{0x5B66,L'x'},{0x4E60,L'x'},{0x6559,L'j'},{0x4E66,L's'},
        {0x7535,L'd'},{0x8111,L'n'},{0x6E38,L'y'},{0x620F,L'x'},{0x89C6,L's'},{0x9891,L'p'},{0x56FE,L't'},
        {0x7247,L'p'},{0x7167,L'z'},{0x673A,L'j'},{0x624B,L's'},
    };
    auto fallback = [](wchar_t ch)->wchar_t {
        int lo=0, hi=(int)(sizeof(kMap)/sizeof(kMap[0]))-1;
        while(lo<=hi){int m=(lo+hi)/2;if(kMap[m].cp==ch)return kMap[m].py;if(kMap[m].cp<ch)lo=m+1;else hi=m-1;}
        return 0;
    };
    std::wstring stem = name;
    size_t dot = stem.rfind(L'.');
    if (dot != std::wstring::npos && dot > 0) stem = stem.substr(0, dot);
    static PinyinDict* dict = pinyin_dict_new();
    if (!dict) return L"";
    auto& pc = pinyinRuntimeCache();
    std::wstring result;
    for (wchar_t ch : stem) {
        if ((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')) { result += towlower(ch); continue; }
        auto cit = pc.find(ch); if (cit != pc.end()) { result += cit->second; continue; }
        char buf[16] = {};
        int len = pinyin_first_letter(dict, (unsigned int)ch, buf, sizeof(buf));
        if (len > 0) { wchar_t py = (wchar_t)buf[0]; result += py; pc[ch] = py; continue; }
        wchar_t py = fallback(ch);
        if (py) { result += py; pc[ch] = py; }
    }
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

// ── 拼音持久化 ──────────────────────────────────────────────
void FileIndex::loadPinyinCache(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb"); if (!f) return;
    uint32_t n; fread(&n, 4, 1, f);
    auto& c = pinyinRuntimeCache();
    for (uint32_t i = 0; i < n && !feof(f); ++i) {
        uint16_t cp; wchar_t py; fread(&cp,2,1,f); fread(&py,2,1,f); c[(wchar_t)cp] = py;
    } fclose(f);
}
void FileIndex::savePinyinCache(const std::string& path) const {
    auto& c = pinyinRuntimeCache(); if (c.empty()) return;
    FILE* f = fopen(path.c_str(), "wb"); if (!f) return;
    uint32_t n = (uint32_t)c.size(); fwrite(&n, 4, 1, f);
    for (const auto& [cp, py] : c) { fwrite(&cp,2,1,f); fwrite(&py,2,1,f); } fclose(f);
}

// ── 二进制缓存 ────────────────────────────────────────────────
static constexpr uint32_t kMagic = 0x46494E44;
static void w16(FILE* f, uint16_t v) { fwrite(&v,2,1,f); }
static void w32(FILE* f, uint32_t v) { fwrite(&v,4,1,f); }
static void w64(FILE* f, uint64_t v) { fwrite(&v,8,1,f); }
static void wW(FILE* f, const std::wstring& s) { uint16_t l=(uint16_t)s.size();w16(f,l);if(l)fwrite(s.data(),2,l,f); }
static uint16_t r16(FILE* f) { uint16_t v;fread(&v,2,1,f);return v; }
static uint32_t r32(FILE* f) { uint32_t v;fread(&v,4,1,f);return v; }
static uint64_t r64(FILE* f) { uint64_t v;fread(&v,8,1,f);return v; }
static std::wstring rW(FILE* f) { uint16_t l=r16(f);std::wstring s(l,L'\0');if(l)fread(&s[0],2,l,f);return s; }

bool FileIndex::saveToFile(const std::string& path) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    FILE* f = fopen(path.c_str(), "wb"); if (!f) return false;
    uint32_t n = (uint32_t)m_pathIndex.size(); w32(f,kMagic); w32(f,n);
    for (const auto& [p, e] : m_pathIndex) { wW(f,e.name);wW(f,e.path);wW(f,e.ext);wW(f,e.pinyin);w64(f,e.size);w64(f,(uint64_t)e.id); }
    fclose(f); return true;
}
bool FileIndex::loadFromFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb"); if (!f) return false;
    uint32_t m = r32(f);
    if (m != kMagic) {
        fseek(f,0,SEEK_SET); fseek(f,0,SEEK_END); long sz=ftell(f);
        if(sz>0){fseek(f,0,SEEK_SET);std::string ts(sz,'\0');fread(&ts[0],1,sz,f);fclose(f);
            int n=MultiByteToWideChar(CP_UTF8,0,ts.c_str(),(int)sz,nullptr,0);
            if(n>0){std::wstring wd(n,L'\0');MultiByteToWideChar(CP_UTF8,0,ts.c_str(),(int)sz,&wd[0],n);
                clear(); size_t p=0;
                while(p<wd.size()){
                    size_t t1=wd.find(L'\t',p),t2=wd.find(L'\t',t1+1),t3=wd.find(L'\t',t2+1),t4=wd.find(L'\t',t3+1),nl=wd.find(L'\n',t4+1);
                    if(t1==std::wstring::npos||t2==std::wstring::npos||t3==std::wstring::npos||t4==std::wstring::npos||nl==std::wstring::npos)break;
                    FileEntry e;e.name=wd.substr(p,t1-p);e.path=wd.substr(t1+1,t2-t1-1);e.ext=wd.substr(t2+1,t3-t2-1);e.pinyin=wd.substr(t3+1,t4-t3-1);
                    e.size=_wcstoui64(wd.substr(t4+1,nl-t4-1).c_str(),nullptr,10);e.nameLow=toLower(e.name);e.id=m_nextId++;
                    m_pathIndex[e.path]=e;m_nameIndex[e.nameLow].push_back(e);if(!e.pinyin.empty()){std::wstring py=toLower(e.pinyin);if(!py.empty())m_pinyinIndex[py].insert(e.path);}
                    p=nl+1;}
                rebuildSortedNames();saveToFile(path);return true;}}
        return false;}
    uint32_t n = r32(f); clear();
    for (uint32_t i=0;i<n;++i){
        FileEntry e;e.name=rW(f);e.path=rW(f);e.ext=rW(f);e.pinyin=rW(f);e.size=r64(f);e.id=(int64_t)r64(f);
        e.nameLow=toLower(e.name); m_pathIndex[e.path]=e;m_nameIndex[e.nameLow].push_back(e);
        if(!e.pinyin.empty()){std::wstring py=toLower(e.pinyin);if(!py.empty())m_pinyinIndex[py].insert(e.path);}
    }
    rebuildSortedNames();if(m_nextId<=n)m_nextId=n+1;fclose(f);return true;
}

} // namespace geofinder
