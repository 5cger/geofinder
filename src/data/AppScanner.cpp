// AppScanner — 哈希表版实现
//
// 后台线程扫描目录，所有文件直接写入 FileIndex。

#include "data/AppScanner.hpp"

#include <windows.h>
#include <cstdio>

#include "utils/StringUtils.hpp"

namespace geofinder {

AppScanner::AppScanner(FileIndex* fileIndex)
    : m_fileIndex(fileIndex)
{
}

AppScanner::~AppScanner()
{
    stop();
}

void AppScanner::setSearchDirectories(const std::vector<std::string>& dirs)
{
    m_searchDirs = dirs;
}

void AppScanner::startBackgroundScan()
{
    if (m_scanning.load()) return;
    m_scanning.store(true);
    m_scanThread = std::thread(&AppScanner::scanWorker, this);
}

void AppScanner::stop()
{
    m_scanning.store(false);
    if (m_scanThread.joinable()) m_scanThread.join();
}

void AppScanner::update()
{
    if (!m_scanning.load() && m_hasPendingResult) {
        ScanResult r = m_pendingResult;
        m_hasPendingResult = false;
        if (m_completeCb) m_completeCb(r);
    }
}

// ── scanWorker ─────────────────────────────────────────────────────

void AppScanner::scanWorker()
{
    ScanResult result;

    for (const auto& dir : m_searchDirs) {
        if (!m_scanning.load()) break;
        if (dir.empty()) continue;
        std::wstring wdir = StringUtils::utf8ToWide(dir);
        if (!wdir.empty()) {
            std::wprintf(L"[Scanner] %ls\n", wdir.c_str());
            if (m_progressCb) m_progressCb(result.totalIndexed, wdir);
            scanDirectory(wdir, result);
        }
    }

    m_pendingResult = result;
    m_hasPendingResult = true;
    m_scanning.store(false);

    std::printf("[Scanner] done: %d scanned, %d indexed, total=%d\n",
                result.totalScanned, result.totalIndexed, m_fileIndex->count());
}

// ── scanDirectory ──────────────────────────────────────────────────

void AppScanner::scanDirectory(const std::wstring& dir, ScanResult& result)
{
    if (!m_scanning.load()) return;
    if (dir.empty()) return;

    std::wstring searchPath = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (!m_scanning.load()) break;

        if (wcscmp(fd.cFileName, L".") == 0 ||
            wcscmp(fd.cFileName, L"..") == 0) continue;

        std::wstring fullPath = dir + L"\\" + fd.cFileName;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                scanDirectory(fullPath, result);
            }
        } else {
            result.totalScanned++;

            FileEntry entry;
            entry.name = fd.cFileName;
            entry.path = fullPath;

            // 扩展名
            std::wstring name(fd.cFileName);
            size_t dot = name.rfind(L'.');
            if (dot != std::wstring::npos) {
                entry.ext = name.substr(dot);
                for (auto& ch : entry.ext) ch = towlower(ch);
            }

            // 文件大小
            ULARGE_INTEGER sz;
            sz.LowPart = fd.nFileSizeLow;
            sz.HighPart = fd.nFileSizeHigh;
            entry.size = sz.QuadPart;

            // 拼音首字母
            entry.pinyin = FileIndex::nameToPinyin(entry.name);

            m_fileIndex->insert(entry);
            result.totalIndexed++;
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

} // namespace geofinder
