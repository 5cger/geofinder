// Application — 异步搜索 + 动画配置集成
#include "app/Application.hpp"
#include "render/CommandBuffer.hpp"
#include "render/OpenGLBackend.hpp"
#include "resource/FontManager.hpp"
#include "resource/ResourceManager.hpp"
#include "system/WindowManager.hpp"
#include "ui/UIManager.hpp"
#include "utils/StringUtils.hpp"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <cstdio>
#include <memory>
#include <thread>
#include <mutex>
#include <windows.h>
#include <shlobj.h>
#include <cwctype>

namespace geofinder {

Application::Application() = default;
Application::~Application() = default;

bool Application::init()
{
    m_winMgr = std::make_unique<WindowManager>(800, 450, "GeoFinder");
    if (!m_winMgr->init()) return false;

    m_backend = std::make_unique<OpenGLBackend>();
    if (!m_backend->init(m_winMgr->getGLFWWindow())) return false;
    m_backend->resize(800, 450);

    m_resMgr = std::make_unique<ResourceManager>();
    m_resMgr->setBackend(m_backend.get());

    m_fontMgr = std::make_unique<FontManager>(m_resMgr.get(), m_backend.get());
    {
        const wchar_t* kPaths[] = {L"C:/Windows/Fonts/msyh.ttc", L"C:/Windows/Fonts/simsun.ttc", L"C:/Windows/Fonts/arial.ttf"};
        bool ok = false;
        for (auto* p : kPaths) if (m_fontMgr->loadFont(p, 48.0f)) { ok = true; break; }
        if (!ok) std::fprintf(stderr, "[App] No font\n");
    }

    // 配置
    {
        wchar_t exeDir[MAX_PATH]; GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
        std::wstring dir(exeDir); size_t s = dir.find_last_of(L"\\/");
        if (s != std::wstring::npos) dir = dir.substr(0, s + 1);
        std::string cfgPath = StringUtils::wideToUtf8(dir) + "geofinder_config.json";
        m_config = std::make_unique<ConfigManager>(cfgPath);
        m_config->load();

        std::vector<std::string> defaults;
        auto addKF = [&](REFKNOWNFOLDERID rfid) {
            wchar_t* raw = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(rfid, 0, nullptr, &raw))) {
                defaults.push_back(StringUtils::wideToUtf8(raw)); CoTaskMemFree(raw);
            }
        };
        addKF(FOLDERID_StartMenu); addKF(FOLDERID_CommonStartMenu);
        addKF(FOLDERID_Desktop); addKF(FOLDERID_Documents); addKF(FOLDERID_Downloads);
        m_config->ensureDefaults(defaults);
        m_cachePath = StringUtils::wideToUtf8(dir) + "geofinder_cache.tsv";
    }

    m_fileIndex = std::make_unique<FileIndex>();
    m_fileIndex->loadFromFile(m_cachePath);
    m_fileIndex->loadPinyinCache(m_cachePath.substr(0, m_cachePath.rfind('.')) + "_pinyin.bin");

    m_scanner = std::make_unique<AppScanner>(m_fileIndex.get());
    m_scanner->setSearchDirectories(m_config->getSearchDirs());
    m_scanner->setScanProgressCallback([this](int idx, const std::wstring& d) {
        if (m_uiMgr) m_uiMgr->getSettingsPage()->setScanProgress(idx, d);
        if (m_windowVisible) m_uiDirty = true;
    });
    m_scanner->setScanCompleteCallback([this](const ScanResult& r) {
        printf("[App] Scan done: %d total=%d\n", r.totalIndexed, m_fileIndex->count());
        m_scanComplete = true; m_scanTotalScanned = r.totalIndexed;
        if (m_uiMgr) m_uiMgr->getSettingsPage()->setScanComplete(m_scanTotalScanned);
        if (!m_cachePath.empty()) {
            m_fileIndex->saveToFile(m_cachePath);
            std::string pyc = m_cachePath.substr(0, m_cachePath.rfind('.')) + "_pinyin.bin";
            m_fileIndex->savePinyinCache(pyc);
        }
        if (m_windowVisible && m_uiMgr) onSearch(m_pendingQuery);
    });
    m_scanner->startBackgroundScan();

    m_fileWatcher = std::make_unique<FileWatcher>();
    for (const auto& d : m_config->getSearchDirs())
        m_fileWatcher->addWatch(StringUtils::utf8ToWide(d));
    m_fileWatcher->setChangeCallback([this](const std::vector<FileChange>& chs) {
        for (const auto& ch : chs) {
            if (ch.type == FileChange::Added || ch.type == FileChange::Modified) {
                FileEntry e; e.name = ch.path.substr(ch.path.rfind(L'\\') + 1);
                e.path = ch.path; size_t dot = e.name.rfind(L'.');
                if (dot != std::wstring::npos) { e.ext = e.name.substr(dot);
                    for (auto& c : e.ext) c = towlower(c); }
                e.pinyin = FileIndex::nameToPinyin(e.name); m_fileIndex->insert(e);
            }
        }
        if (m_windowVisible) m_uiDirty = true;
    });
    m_fileWatcher->start();

    // UI + 动画速度
    m_iconCache = std::make_unique<IconCache>(m_resMgr.get());
    m_uiMgr = std::make_unique<UIManager>(m_fontMgr.get(), m_config.get(), m_iconCache.get());
    m_uiMgr->setInputOnChangeCallback([this](const std::wstring& q) { triggerSearch(q); });
    m_uiMgr->setLaunchCallback([this](const SearchResultEntry& e) { onLaunch(e); });
    m_uiMgr->setSettingsRescanCallback([this]() { doRescan(); });
    if (m_uiMgr->getResultList()) m_uiMgr->getResultList()->setAnimSpeed(m_config->getAnimSpeed());
    if (m_uiMgr->getSettingsPage())
        m_uiMgr->getSettingsPage()->setOnAnimSpeedChange([this](float v) {
            if (m_uiMgr->getResultList()) m_uiMgr->getResultList()->setAnimSpeed(v);
        });

    m_winMgr->setCloseCallback([this]() { m_running = false; });
    m_winMgr->setKeyCallback([this](int k, int s, int a, int m) { onKey(k, s, a, m); });
    m_winMgr->setCharCallback([this](unsigned int c) { onChar(c); });
    m_winMgr->setMouseCallback([this](int b, int a, int m) { onMouse(b, a, m); });
    m_winMgr->setCursorCallback([this](double x, double y) { onCursor(x, y); });

    // 热键
    m_hotkeyMgr = std::make_unique<HotkeyManager>();
    m_hotkeyMgr->setCallback([this]() {
        if (m_windowVisible) { m_winMgr->hide(); m_windowVisible = false; }
        else { m_winMgr->show(); m_windowVisible = true; m_uiDirty = true; }
    });
    HotkeyConfig hk;
    if (!m_hotkeyMgr->registerHotkey(hk)) printf("[App] Hotkey failed\n");

    // 托盘
    m_trayMgr = std::make_unique<TrayManager>();
    if (m_trayMgr->init()) {
        m_trayMgr->setCallback([this](TrayManager::Event evt) {
            switch (evt) {
            case TrayManager::Event::ShowWindow:
                if (!m_windowVisible) { m_winMgr->show(); m_windowVisible = true; m_uiDirty = true; } break;
            case TrayManager::Event::Exit: m_running = false; break;
            case TrayManager::Event::OpenSettings: openSettings(); break;
            default: break;
            }
        });
    }

    // IME
    m_ime = std::make_unique<IMEWrapper>();
    HWND hwnd = glfwGetWin32Window(m_winMgr->getGLFWWindow());
    if (hwnd) {
        m_ime->setWindow(hwnd);
        m_ime->setCallback([this](const std::wstring& preedit) {
            if (m_uiMgr) m_uiMgr->setSearchQuery(preedit.empty() ? m_pendingQuery : preedit);
        });
    }

    printf("[App] Ready — Alt+Space\n");
    return true;
}

void Application::shutdown()
{
    if (m_searchThread.joinable()) m_searchThread.join();
    if (m_scanner) m_scanner->stop();
    m_hotkeyMgr.reset(); m_trayMgr.reset();
    if (m_fileWatcher) { m_fileWatcher->stop(); m_fileWatcher.reset(); }
    m_ime.reset();
    if (m_config) m_config->save();
    if (m_fileIndex && !m_cachePath.empty()) {
        m_fileIndex->saveToFile(m_cachePath);
        std::string pyc = m_cachePath.substr(0, m_cachePath.rfind('.')) + "_pinyin.bin";
        m_fileIndex->savePinyinCache(pyc);
    }
    m_config.reset(); m_scanner.reset(); m_fileIndex.reset();
    m_uiMgr.reset(); m_fontMgr.reset(); m_resMgr.reset();
    m_backend.reset(); m_winMgr.reset();
}

int Application::run()
{
    if (!init()) return 1;
    m_running = true;

    auto lastTime = std::chrono::steady_clock::now();
    auto lastRefresh = std::chrono::steady_clock::now();

    while (m_running && !m_winMgr->shouldClose()) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (m_ime && m_ime->handleImeMessage(msg.message, msg.wParam, msg.lParam)) continue;
            if (m_hotkeyMgr && m_hotkeyMgr->checkMessage(msg)) continue;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        glfwPollEvents();

        auto now = std::chrono::steady_clock::now();
        float delta = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        if (m_scanner) m_scanner->update();
        if (m_fileWatcher) m_fileWatcher->processChanges();
        if (!m_windowVisible) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }

        if (m_searchPending && std::chrono::duration<float>(now - m_lastInputTime).count() >= 0.05f) {
            m_searchPending = false;
            if (m_searchThread.joinable()) m_searchThread.join();
            std::wstring q = m_pendingQuery;
            int gen = m_resultGeneration.load() + 1;
            bool scanDone = m_scanComplete;  // 捕获扫描状态

            // ── 第一步：立即过滤已有结果（不闪烁，渐窄） ──────
            {
                std::lock_guard<std::mutex> lg(m_resultMutex);
                if (!m_sharedResults.empty()) {
                    std::wstring qLow = q;
                    for (auto& c : qLow) c = towlower(c);
                    std::vector<SearchResultEntry> filtered;
                    filtered.reserve(m_sharedResults.size());
                    for (const auto& r : m_sharedResults) {
                        std::wstring n = r.name;
                        for (auto& c : n) c = towlower(c);
                        if (n.find(qLow) != std::wstring::npos) {
                            filtered.push_back(r);
                        }
                    }
                    if (filtered.size() != m_sharedResults.size() || !q.empty()) {
                        m_sharedResults = std::move(filtered);
                        m_resultGeneration.store(gen++);
                        m_displayedGeneration = -1; // 强制前台刷新
                    }
                }
            }

            // ── 第二步：后台全量搜索 ──────────────────────────
            m_searchThread = std::thread([this, q, gen, scanDone]() {
                std::wstring clean = q, ext;
                size_t at = clean.find(L'@');
                if (at != std::wstring::npos) {
                    size_t cl = clean.find(L':', at);
                    if (cl != std::wstring::npos && cl > at + 1) {
                        ext = L"." + clean.substr(at + 1, cl - at - 1);
                        for (auto& c : ext) c = towlower(c);
                        clean = clean.substr(0, at);
                    }
                }
                auto results = m_fileIndex->search(clean, ext, 500);
                // 空查询时仅显示可执行文件（lnk 优先）
                if (clean.empty() && ext.empty()) {
                    auto isApp = [](const std::wstring& n) -> int { // 0=none, 1=lnk, 2=exe
                        size_t d = n.rfind(L'.');
                        if (d == std::wstring::npos) return 0;
                        std::wstring e = n.substr(d);
                        for (auto& c : e) c = towlower(c);
                        if (e == L".lnk") return 1;
                        if (e == L".exe" || e == L".bat" || e == L".cmd") return 2;
                        return 0;
                    };
                    std::vector<SearchResultEntry> lnks, exes;
                    for (auto& r : results) {
                        int t = isApp(r.name);
                        if (!t) t = isApp(r.path);
                        if (t == 1) lnks.push_back(std::move(r));
                        else if (t == 2) exes.push_back(std::move(r));
                    }
                    results.clear();
                    results.reserve(lnks.size() + exes.size());
                    for (auto& r : lnks) results.push_back(std::move(r));
                    for (auto& r : exes) results.push_back(std::move(r));
                }
                // 短暂让步，避免前台 UI 卡顿
                std::this_thread::yield();
                std::lock_guard<std::mutex> lg(m_resultMutex);
                if (m_resultGeneration.load() < gen) {
                    m_sharedResults = std::move(results);
                    m_resultGeneration.store(gen);
                }
            });
        }

        // ── 前台读取共享结果列表（无锁等待） ───────────────────
        int curGen = m_resultGeneration.load();
        if (curGen != m_displayedGeneration) {
            std::vector<SearchResultEntry> results;
            {
                std::lock_guard<std::mutex> lg(m_resultMutex);
                results = m_sharedResults;
            }
            m_displayedGeneration = curGen;
            onSearchComplete(std::move(results));
        }

        if (m_scanner && m_scanner->isScanning() &&
            std::chrono::duration<float>(now - lastRefresh).count() >= 0.5f) {
            lastRefresh = now;
            if (m_pendingQuery.empty() && !m_searchPending) onSearch(L"");
        }

        m_uiMgr->update(delta);
        bool needPaint = m_uiDirty || m_uiMgr->isDirty() || m_uiMgr->getAnimation().hasActive();

        if (needPaint) {
            m_backend->beginFrame();
            CommandBuffer cmdBuf;
            { RenderCommand c; RectOp bg;
              bg.pos = glm::vec2(0,0); bg.size = glm::vec2(800,450);
              bg.color = glm::vec4(0.10f,0.10f,0.12f,1); c.op = bg; cmdBuf.add(c); }
            m_uiMgr->paint(cmdBuf);
            m_uiDirty = false; m_uiMgr->clearDirty();
            m_backend->execute(cmdBuf); m_backend->endFrame(); m_backend->present();
            m_lastPaintTime = std::chrono::steady_clock::now();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        m_resMgr->recycleTemporaries();
        m_backend->collectGarbage(m_frameIndex);
        m_frameIndex++;
    }

    shutdown();
    return 0;
}

void Application::onKey(int key, int sc, int act, int mods)
{
    if (act != GLFW_PRESS && act != GLFW_REPEAT) return;
    if (key == GLFW_KEY_ESCAPE) {
        if (m_uiMgr && !m_uiMgr->onKeyEvent({key, sc, act, mods}))
            { m_winMgr->hide(); m_windowVisible = false; }
        return;
    }
    if (key == GLFW_KEY_O && (mods & GLFW_MOD_CONTROL) && m_uiMgr) {
        const auto* sel = m_uiMgr->getSelectedResult();
        if (sel && !sel->path.empty()) {
            // SHOpenFolderAndSelectItems \u4e3b\u8981\u65b9\u5f0f, ShellExecute \u5907\u7528
            PIDLIST_ABSOLUTE pidl = nullptr;
            if (SUCCEEDED(SHParseDisplayName(sel->path.c_str(), nullptr, &pidl, 0, nullptr))) {
                SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
                ILFree(pidl);
            } else {
                std::wstring cmd = L"/select,\"" + sel->path + L"\"";
                ShellExecuteW(nullptr, L"open", L"explorer.exe", cmd.c_str(), nullptr, SW_SHOW);
            }
        }
        return;
    }
    if (m_uiMgr) m_uiMgr->onKeyEvent({key, sc, act, mods});
}

void Application::onChar(uint32_t cp) { if (m_uiMgr) m_uiMgr->onCharEvent({cp}); }

void Application::onMouse(int btn, int act, int mods) {
    double cx = 0, cy = 0;
    GLFWwindow* w = m_winMgr ? m_winMgr->getGLFWWindow() : nullptr;
    if (w) glfwGetCursorPos(w, &cx, &cy);
    if (m_winMgr) m_winMgr->onWindowDrag(btn, act, cx, cy);
    (void)mods;
}
void Application::onCursor(double x, double y) { if (m_winMgr) m_winMgr->onWindowDragMove(x, y); }

void Application::triggerSearch(const std::wstring& q) {
    m_lastInputTime = std::chrono::steady_clock::now();
    m_pendingQuery = q; m_searchPending = true;
}

void Application::onSearchComplete(std::vector<SearchResultEntry> results)
{
    if (!m_uiMgr) return;
    int total = m_fileIndex ? m_fileIndex->count() : 0;
    wchar_t status[192];
    if (!m_scanComplete)
        _snwprintf(status, 192, L"搜索中... %d 结果 | 正在扫描 %d 文件", (int)results.size(), total);
    else if (total == 0)
        wcscpy_s(status, L"未索引到文件 — /rescan 重新扫描 | /add D:\\\\路径");
    else
        _snwprintf(status, 192, L"已索引 %d 文件 | %d 结果  ↑↓选择  Enter打开  Ctrl+O定位  Esc退出", total, (int)results.size());
    m_uiMgr->setSearchResults(results);
    m_uiMgr->setStatusText(status);
    m_uiDirty = true;
}

void Application::onSearch(const std::wstring& query)
{
    if (!m_uiMgr) return;
    if (!query.empty() && query[0] == L'/') {
        if (query.rfind(L"/add ", 0) == 0) {
            std::wstring d = query.substr(5);
            while (!d.empty() && d.back() == L' ') d.pop_back();
            if (!d.empty() && m_config) {
                m_config->addSearchDir(StringUtils::wideToUtf8(d)); m_config->save();
                if (m_scanner) m_scanner->setSearchDirectories(m_config->getSearchDirs());
                m_uiMgr->setStatusText(L"已添加: " + d);
            } return;
        }
        if (query.rfind(L"/remove ", 0) == 0) {
            std::wstring d = query.substr(8);
            while (!d.empty() && d.back() == L' ') d.pop_back();
            if (!d.empty() && m_config) {
                m_config->removeSearchDir(StringUtils::wideToUtf8(d)); m_config->save();
                if (m_scanner) m_scanner->setSearchDirectories(m_config->getSearchDirs());
                m_uiMgr->setStatusText(L"已删除: " + d);
            } return;
        }
        if (query == L"/rescan") { doRescan(); return; }
        if (query == L"/dirs") { openSettings(); return; }
    }

    std::wstring clean = query, extFilter;
    size_t at = query.find(L'@');
    if (at != std::wstring::npos) {
        size_t cl = query.find(L':', at);
        if (cl != std::wstring::npos && cl > at + 1) {
            extFilter = query.substr(at + 1, cl - at - 1);
            bool valid = !extFilter.empty();
            for (auto& c : extFilter) { c = towlower(c); if (!iswalnum(c)) valid = false; }
            if (valid) { extFilter = L"." + extFilter; clean = query.substr(0, at); }
            else extFilter.clear();
        }
    }
    while (!clean.empty() && clean.front() == L' ') clean.erase(0, 1);
    while (!clean.empty() && clean.back() == L' ') clean.pop_back();

    if (clean.size() >= 3 && clean.find(L":\\") != std::wstring::npos) {
        std::wstring dp = clean;
        if (dp.back() != L'\\') dp += L'\\';
        DWORD attr = GetFileAttributesW(dp.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring sp = dp + L"*"; WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW(sp.c_str(), &fd);
            std::vector<SearchResultEntry> dr; int idi = 0;
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                    SearchResultEntry r; r.id = idi++; r.name = fd.cFileName;
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) r.name += L"\\";
                    r.path = dp + fd.cFileName;
                    ULARGE_INTEGER sz; sz.LowPart = fd.nFileSizeLow; sz.HighPart = fd.nFileSizeHigh;
                    r.fileSize = sz.QuadPart;
                    if (!extFilter.empty()) {
                        size_t d = r.name.rfind(L'.');
                        if (d != std::wstring::npos) {
                            std::wstring fe = r.name.substr(d);
                            for (auto& c : fe) c = towlower(c);
                            if (fe != extFilter) continue;
                        } else if (extFilter != L".*") continue;
                    }
                    dr.push_back(r);
                } while (FindNextFileW(h, &fd) && dr.size() < 200);
                FindClose(h);
            }
            m_uiMgr->setSearchResults(dr);
            m_uiMgr->setStatusText(L"浏览: " + dp); return;
        }
    }

    auto results = m_fileIndex->search(clean, extFilter, 500);
    // \u7a7a\u67e5\u8be2\u65f6\u4ec5\u663e\u793a\u53ef\u6267\u884c\u6587\u4ef6\uff08lnk \u4f18\u5148\uff09
    if (clean.empty() && extFilter.empty()) {
        auto isApp = [](const std::wstring& n) -> int {
            size_t d = n.rfind(L'.');
            if (d == std::wstring::npos) return 0;
            std::wstring e = n.substr(d);
            for (auto& c : e) c = towlower(c);
            if (e == L".lnk") return 1;
            if (e == L".exe" || e == L".bat" || e == L".cmd") return 2;
            return 0;
        };
        std::vector<SearchResultEntry> lnks, exes;
        for (auto& r : results) {
            int t = isApp(r.name);
            if (!t) t = isApp(r.path);
            if (t == 1) lnks.push_back(std::move(r));
            else if (t == 2) exes.push_back(std::move(r));
        }
        results.clear();
        results.reserve(lnks.size() + exes.size());
        for (auto& r : lnks) results.push_back(std::move(r));
        for (auto& r : exes) results.push_back(std::move(r));
    }
    int total = m_fileIndex->count();
    wchar_t status[192];
    if (!extFilter.empty() && clean.empty())
        _snwprintf(status, 192, L"@%ls: %zu 个文件 | 输入关键词搜索", extFilter.c_str()+1, results.size());
    else if (!extFilter.empty())
        _snwprintf(status, 192, L"@%ls: %zu 结果 | %d 文件", extFilter.c_str()+1, results.size(), total);
    else if (!m_scanComplete)
        _snwprintf(status, 192, L"正在扫描... %d 文件 | @pdf:", total);
    else if (total == 0)
        wcscpy_s(status, L"未索引到文件 — /rescan | /add D:\\\\路径");
    else
        _snwprintf(status, 192, L"%d 文件 | @pdf:  ↑↓选择  Enter打开  Ctrl+O定位  Esc退出", total);
    m_uiMgr->setSearchResults(results);
    m_uiMgr->setStatusText(status);
}

void Application::onLaunch(const SearchResultEntry& entry)
{
    printf("[App] Launch: %ls\n", entry.name.c_str());
    ShellExecuteW(nullptr, L"open", entry.path.c_str(), nullptr, nullptr, SW_SHOW);
    m_winMgr->hide(); m_windowVisible = false;
}

void Application::openSettings() {
    if (!m_uiMgr) return;
    if (!m_windowVisible) { m_winMgr->show(); m_windowVisible = true; }
    m_uiMgr->showSettingsPage(); m_uiMgr->markDirty();
}

void Application::doRescan() {
    if (!m_scanner) return;
    m_scanner->stop();
    m_fileIndex->clear(); m_scanComplete = false; m_scanTotalScanned = 0;
    if (m_config) m_scanner->setSearchDirectories(m_config->getSearchDirs());
    if (m_uiMgr && m_uiMgr->getSettingsPage()) m_uiMgr->getSettingsPage()->setScanning(true);
    m_scanner->startBackgroundScan();
    if (m_uiMgr) { m_uiMgr->setSearchResults({}); m_uiMgr->setStatusText(L"扫描中..."); m_uiMgr->markDirty(); }
}

} // namespace geofinder
