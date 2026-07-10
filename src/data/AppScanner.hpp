// AppScanner — 目录扫描器（哈希表版）
//
// 后台线程扫描目录，将文件直接写入 FileIndex 哈希表。
// 不再依赖 SQLite / PinyinConverter / LnkResolver。
#pragma once

#include "data/FileIndex.hpp"
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace geofinder {

struct ScanResult {
    int totalScanned = 0;
    int totalIndexed = 0;
};

class AppScanner {
public:
    explicit AppScanner(FileIndex* fileIndex);
    ~AppScanner();

    void setSearchDirectories(const std::vector<std::string>& dirs);

    void startBackgroundScan();
    void stop();
    bool isScanning() const { return m_scanning.load(); }

    using ScanCompleteCallback = std::function<void(const ScanResult&)>;
    void setScanCompleteCallback(ScanCompleteCallback cb) { m_completeCb = std::move(cb); }

    using ScanProgressCallback = std::function<void(int filesScanned, const std::wstring& currentDir)>;
    void setScanProgressCallback(ScanProgressCallback cb) { m_progressCb = std::move(cb); }
    void update();

private:
    FileIndex* m_fileIndex;
    std::vector<std::string> m_searchDirs;
    std::thread m_scanThread;
    std::atomic<bool> m_scanning{false};
    std::atomic<bool> m_hasPendingResult{false};
    ScanResult m_pendingResult;
    ScanCompleteCallback m_completeCb;
    ScanProgressCallback m_progressCb;

    void scanWorker();
    void scanSingleDir(const std::wstring& dir, std::vector<std::wstring>& subdirs, ScanResult& result);
};

} // namespace geofinder
