// 参考蓝图 §5.15 — FileWatcher IOCP 实现
//
// 统一 IOCP 端口，工作线程阻塞 GetQueuedCompletionStatus。
// 每次通知后重新投递 ReadDirectoryChangesW（armWatch）。

#include "system/FileWatcher.hpp"
#include <cstdio>
#include <algorithm>

namespace geofinder {

static constexpr ULONG_PTR WATCH_KEY = 0x1;

FileWatcher::FileWatcher() = default;

FileWatcher::~FileWatcher() { stop(); }

bool FileWatcher::addWatch(const std::wstring& directory)
{
    HANDLE hDir = CreateFileW(
        directory.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);

    if (hDir == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "[FileWatcher] cannot open: %ls (err=%lu)\n",
                     directory.c_str(), GetLastError());
        return false;
    }

    Watch watch;
    watch.hDir = hDir;
    watch.path = directory;

    if (!m_ioPort) {
        m_ioPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (!m_ioPort) { CloseHandle(hDir); return false; }
    }

    CreateIoCompletionPort(hDir, m_ioPort, WATCH_KEY, 0);
    m_watches.push_back(std::move(watch));
    armWatch(m_watches.back());

    std::printf("[FileWatcher] watching: %ls\n", directory.c_str());
    return true;
}

void FileWatcher::removeWatch(const std::wstring& directory)
{
    m_watches.erase(std::remove_if(m_watches.begin(), m_watches.end(),
        [&](const Watch& w) {
            if (w.path == directory) {
                CloseHandle(w.hDir);
                return true;
            }
            return false;
        }), m_watches.end());
}

bool FileWatcher::armWatch(Watch& watch)
{
    memset(&watch.overlapped, 0, sizeof(OVERLAPPED));
    DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME |
                   FILE_NOTIFY_CHANGE_DIR_NAME |
                   FILE_NOTIFY_CHANGE_LAST_WRITE;
    BOOL ok = ReadDirectoryChangesW(watch.hDir, watch.buffer,
        sizeof(watch.buffer), TRUE, filter, nullptr,
        &watch.overlapped, nullptr);
    watch.pending = ok;
    return ok;
}

void FileWatcher::start()
{
    if (m_running) return;
    m_running = true;
    m_worker = std::thread(&FileWatcher::workerLoop, this);
}

void FileWatcher::stop()
{
    if (!m_running) return;
    m_running = false;
    if (m_ioPort) PostQueuedCompletionStatus(m_ioPort, 0, 0, nullptr);
    if (m_worker.joinable()) m_worker.join();
    for (auto& w : m_watches)
        if (w.hDir != INVALID_HANDLE_VALUE) CloseHandle(w.hDir);
    m_watches.clear();
    if (m_ioPort) { CloseHandle(m_ioPort); m_ioPort = nullptr; }
}

void FileWatcher::workerLoop()
{
    while (m_running) {
        DWORD bytesReturned = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED overlapped = nullptr;

        BOOL ok = GetQueuedCompletionStatus(
            m_ioPort, &bytesReturned, &key, &overlapped, INFINITE);

        if (!m_running) break;
        if (!ok || bytesReturned == 0) continue;

        for (auto& w : m_watches) {
            if (&w.overlapped == overlapped) {
                processNotification(w, bytesReturned);
                armWatch(w);  // 重新投递！
                break;
            }
        }
    }
}

void FileWatcher::processNotification(Watch& watch, DWORD bytesReturned)
{
    BYTE* ptr = watch.buffer;
    DWORD remaining = bytesReturned;
    std::vector<FileChange> changes;

    while (remaining > 0) {
        auto* info = (FILE_NOTIFY_INFORMATION*)ptr;
        FileChange change;
        change.path = watch.path + L"\\" +
            std::wstring(info->FileName, info->FileNameLength / sizeof(wchar_t));

        switch (info->Action) {
        case FILE_ACTION_ADDED:          change.type = FileChange::Added; break;
        case FILE_ACTION_REMOVED:        change.type = FileChange::Removed; break;
        case FILE_ACTION_MODIFIED:       change.type = FileChange::Modified; break;
        case FILE_ACTION_RENAMED_OLD_NAME: change.type = FileChange::Renamed; break;
        case FILE_ACTION_RENAMED_NEW_NAME: change.type = FileChange::Added; break;
        default: break;
        }
        changes.push_back(change);

        if (info->NextEntryOffset == 0) break;
        ptr += info->NextEntryOffset;
        remaining -= info->NextEntryOffset;
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        for (auto& c : changes) m_changeQueue.push_back(c);
    }
}

void FileWatcher::processChanges()
{
    std::vector<FileChange> changes;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_changeQueue.empty()) return;
        changes.assign(m_changeQueue.begin(), m_changeQueue.end());
        m_changeQueue.clear();
    }
    if (m_cb && !changes.empty()) m_cb(changes);
}

} // namespace geofinder
