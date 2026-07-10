// Application — 主应用（异步搜索 + 动画配置集成）
#pragma once

#include <memory>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <mutex>
#include <vector>

#include "render/ResourceHandle.hpp"
#include "render/IRenderBackend.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/FontManager.hpp"
#include "resource/IconCache.hpp"
#include "system/WindowManager.hpp"
#include "system/HotkeyManager.hpp"
#include "system/TrayManager.hpp"
#include "system/FileWatcher.hpp"
#include "system/IMEWrapper.hpp"
#include "ui/UIManager.hpp"
#include "data/DataTypes.hpp"
#include "data/FileIndex.hpp"
#include "data/AppScanner.hpp"
#include "data/ConfigManager.hpp"

namespace geofinder {

class Application {
public:
    Application();
    ~Application();
    int run();
    bool init();
    void shutdown();

private:
    // 异步搜索（生成计数避免阻塞）
    std::thread m_searchThread;
    std::mutex m_searchMutex;
    std::vector<SearchResultEntry> m_searchResults;
    bool m_searchDone = false;
    std::atomic<int> m_searchGeneration{0};
    int m_lastCompletedGen = 0;

    // 状态
    bool m_uiDirty = false;
    uint64_t m_frameIndex = 0;
    bool m_running = false;
    bool m_windowVisible = false;
    float m_windowScale = 1.0f;
    float m_windowOpacity = 1.0f;
    bool m_scanComplete = false;
    int m_scanTotalScanned = 0;

    // 搜索防抖
    std::chrono::steady_clock::time_point m_lastInputTime;
    std::chrono::steady_clock::time_point m_lastPaintTime;
    bool m_searchPending = false;
    std::wstring m_pendingQuery;

    // 子系统
    std::unique_ptr<UIManager>        m_uiMgr;
    std::unique_ptr<ResourceManager>  m_resMgr;
    std::unique_ptr<FontManager>      m_fontMgr;
    std::unique_ptr<IconCache>        m_iconCache;
    std::unique_ptr<IRenderBackend>   m_backend;
    std::unique_ptr<WindowManager>    m_winMgr;
    std::unique_ptr<HotkeyManager>    m_hotkeyMgr;
    std::unique_ptr<TrayManager>      m_trayMgr;
    std::unique_ptr<FileWatcher>      m_fileWatcher;
    std::unique_ptr<IMEWrapper>       m_ime;
    ResourceHandle m_testTexture = kInvalidHandle;
    std::string m_cachePath;
    std::unique_ptr<ConfigManager>    m_config;
    std::unique_ptr<FileIndex>        m_fileIndex;
    std::unique_ptr<AppScanner>       m_scanner;

    // 方法
    void onKey(int key, int scancode, int action, int mods);
    void onChar(uint32_t codepoint);
    void onMouse(int button, int action, int mods);
    void onCursor(double x, double y);
    void triggerSearch(const std::wstring& query);
    void onSearchComplete(std::vector<SearchResultEntry> results);
    void onSearch(const std::wstring& query);
    void onLaunch(const SearchResultEntry& entry);
    void openSettings();
    void doRescan();
};

} // namespace geofinder
