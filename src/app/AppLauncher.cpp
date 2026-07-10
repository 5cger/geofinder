// 参考蓝图 §5.23 — AppLauncher 实现

#include "app/AppLauncher.hpp"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstdio>

namespace geofinder {

AppLauncher::AppLauncher() = default;
AppLauncher::~AppLauncher() = default;

bool AppLauncher::launch(const SearchResultEntry& entry)
{
    if (entry.isUWP && !entry.appUserModelId.empty()) {
        return launchUWP(entry.appUserModelId);
    }

    // 普通 exe（从 path 提取参数）
    return launchExe(entry.path, L"", L"");
}

bool AppLauncher::launchExe(const std::wstring& path,
                             const std::wstring& args,
                             const std::wstring& workDir)
{
    // 使用 ShellExecuteEx 启动（支持工作目录和参数）
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_DEFAULT;
    sei.lpVerb = L"open";
    sei.lpFile = path.c_str();
    sei.lpParameters = args.empty() ? nullptr : args.c_str();
    sei.lpDirectory = workDir.empty() ? nullptr : workDir.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&sei)) {
        std::printf("[AppLauncher] launched: %ls\n", path.c_str());
        return true;
    }

    // ShellExecuteEx 失败 → 尝试 CreateProcess
    std::wstring cmdLine = L"\"" + path + L"\"";
    if (!args.empty()) {
        cmdLine += L" " + args;
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    // CreateProcess 需要可写命令行缓冲区
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr,
                       FALSE, 0, nullptr,
                       workDir.empty() ? nullptr : workDir.c_str(),
                       &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        std::printf("[AppLauncher] CreateProcess: %ls\n", path.c_str());
        return true;
    }

    std::fprintf(stderr, "[AppLauncher] launch failed: %ls (err=%lu)\n",
                 path.c_str(), GetLastError());
    return false;
}

bool AppLauncher::launchUWP(const std::wstring& appUserModelId)
{
    // 通过 IApplicationActivationManager 启动 UWP
    // 需要 COM 已初始化
    using IApplicationActivationManager = struct IApplicationActivationManager;

    // CLSID_ApplicationActivationManager
    // {45BA127D-10A8-46EA-8AB7-56EA9078943C}
    const CLSID CLSID_AAM = {0x45BA127D, 0x10A8, 0x46EA,
                             {0x8A, 0xB7, 0x56, 0xEA, 0x90, 0x78, 0x94, 0x3C}};
    // IID_IApplicationActivationManager
    const IID IID_IAAM = {0x2e941141, 0x7f97, 0x4756,
                          {0xba, 0x1d, 0x9d, 0xec, 0xde, 0x89, 0x4a, 0x3d}};

    // 简化的 IApplicationActivationManager 接口（只取 ActivateApplication）
    struct IAAM_VTBL;
    struct IAAM {
        IAAM_VTBL* lpVtbl;
    };
    struct IAAM_VTBL {
        HRESULT (STDMETHODCALLTYPE* QueryInterface)(IAAM*, REFIID, void**);
        ULONG   (STDMETHODCALLTYPE* AddRef)(IAAM*);
        ULONG   (STDMETHODCALLTYPE* Release)(IAAM*);
        HRESULT (STDMETHODCALLTYPE* ActivateApplication)(
            IAAM*, LPCWSTR appUserModelId, LPCWSTR arguments,
            DWORD options, DWORD* processId);
    };

    IAAM* aam = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_AAM, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_IAAM, (void**)&aam);
    if (FAILED(hr) || !aam) {
        // UWP 启动不可用（非 Win8+ 或权限不足）
        std::fprintf(stderr, "[AppLauncher] UWP activation unavailable (hr=0x%lx)\n",
                     static_cast<unsigned long>(hr));
        return false;
    }

    DWORD pid = 0;
    hr = aam->lpVtbl->ActivateApplication(aam, appUserModelId.c_str(),
                                          nullptr, 0, &pid);
    aam->lpVtbl->Release(aam);

    if (SUCCEEDED(hr)) {
        std::printf("[AppLauncher] UWP launched: %ls (pid=%lu)\n",
                    appUserModelId.c_str(), pid);
        return true;
    }

    std::fprintf(stderr, "[AppLauncher] UWP launch failed: %ls (hr=0x%lx)\n",
                 appUserModelId.c_str(), static_cast<unsigned long>(hr));
    return false;
}

} // namespace geofinder
