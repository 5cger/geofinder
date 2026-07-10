// 参考蓝图 §5.15 — FileWatcher IOCP 文件监控
//
// 基于 IOCP 的异步目录监控。每目录一个 Watch，统一 CompletionPort。
// 工作线程阻塞等待 GetQueuedCompletionStatus，主线程处理变更队列。
#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include <functional>

namespace geofinder {

struct FileChange {
    enum Type { Added, Removed, Modified, Renamed };
    Type type;
    std::wstring path;
};

class FileWatcher {
public:
    using ChangeCallback = std::function<void(const std::vector<FileChange>&)>;

    FileWatcher();
    ~FileWatcher();

    bool addWatch(const std::wstring& directory);
    void removeWatch(const std::wstring& directory);

    void start();
    void stop();

    /// 主线程每帧调用：获取并处理累积的变更通知
    void processChanges();

    void setChangeCallback(ChangeCallback cb) { m_cb = std::move(cb); }

private:
    struct Watch {
        HANDLE hDir = INVALID_HANDLE_VALUE;
        std::wstring path;
        OVERLAPPED overlapped = {};
        BYTE buffer[4096] = {};
        bool pending = false;
    };

    std::vector<Watch> m_watches;
    std::thread m_worker;
    std::atomic<bool> m_running{false};
    HANDLE m_ioPort = nullptr;

    std::mutex m_queueMutex;
    std::deque<FileChange> m_changeQueue;
    ChangeCallback m_cb;

    void workerLoop();
    bool armWatch(Watch& watch);
    void processNotification(Watch& watch, DWORD bytesReturned);
};

} // namespace geofinder
