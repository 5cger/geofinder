// AppScanner — BFS 迭代扫描（避免栈溢出 + 跨线程安全）
#include "data/AppScanner.hpp"
#include <windows.h>
#include <cstdio>
#include "utils/StringUtils.hpp"

namespace geofinder {

AppScanner::AppScanner(FileIndex* idx) : m_fileIndex(idx) {}
AppScanner::~AppScanner() { stop(); }

void AppScanner::setSearchDirectories(const std::vector<std::string>& d) { m_searchDirs = d; }
void AppScanner::startBackgroundScan() {
    if (m_scanning.load()) return;
    m_scanning.store(true);
    m_scanThread = std::thread(&AppScanner::scanWorker, this);
}
void AppScanner::stop() {
    m_scanning.store(false);
    if (m_scanThread.joinable()) m_scanThread.join();
}
void AppScanner::update() {
    if (!m_scanning.load() && m_hasPendingResult.load()) {
        ScanResult r = m_pendingResult; m_hasPendingResult.store(false);
        printf("[AppScanner] update: firing complete callback\n");
        if (m_completeCb) m_completeCb(r);
    }
}

void AppScanner::scanWorker() {
    ScanResult result;
    std::vector<std::wstring> dirs;
    for (const auto& d : m_searchDirs)
        if (!d.empty()) dirs.push_back(StringUtils::utf8ToWide(d));

    for (size_t di = 0; di < dirs.size() && m_scanning.load(); ++di) {
        std::wprintf(L"[Scanner] %ls\n", dirs[di].c_str());
        // 手动栈 DFS（内存 O(depth)，非递归不爆栈）
        std::vector<std::wstring> stack;
        stack.push_back(dirs[di]);
        int dirCount = 0;
        while (!stack.empty() && m_scanning.load()) {
            std::wstring cur = std::move(stack.back());
            stack.pop_back();
            if (++dirCount % 200 == 0 && m_progressCb) m_progressCb(result.totalIndexed, cur);
            scanSingleDir(cur, stack, result);
        }
    }
    m_pendingResult = result; m_hasPendingResult = true; m_scanning.store(false);
    std::printf("[Scanner] done: %d files, total=%d\n", result.totalIndexed, m_fileIndex->count());
}

void AppScanner::scanSingleDir(const std::wstring& dir,
                                std::vector<std::wstring>& q, ScanResult& r) {
    std::wstring sp = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(sp.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!m_scanning.load()) break;
        if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 ||
            (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))) continue;
        std::wstring full = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) q.push_back(full);
        } else {
            r.totalScanned++;
            FileEntry e; e.name = fd.cFileName; e.path = full;
            size_t dot = e.name.rfind(L'.');
            if (dot != std::wstring::npos) { e.ext = e.name.substr(dot); for (auto& c:e.ext)c=towlower(c); }
            ULARGE_INTEGER sz; sz.LowPart=fd.nFileSizeLow;sz.HighPart=fd.nFileSizeHigh;e.size=sz.QuadPart;
            e.pinyin = FileIndex::nameToPinyin(e.name);
            m_fileIndex->insert(e);
            r.totalIndexed++;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

} // namespace geofinder
