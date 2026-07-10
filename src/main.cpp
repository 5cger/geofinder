// GeoFinder — Windows 应用启动器
// 参考蓝图 §6 main.cpp
//
// COM 在此统一初始化（COINIT_APARTMENTTHREADED），
// LnkResolver / AppScanner / UWP 等模块不再各自初始化。

#include "app/Application.hpp"

#include <windows.h>
#include <cstdio>

int main() {
    // 单实例检查：已存在则激活已有窗口并退出
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"GeoFinder_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hwnd = FindWindowW(nullptr, L"GeoFinder");
        if (hwnd) {
            SetForegroundWindow(hwnd);
            ShowWindow(hwnd, SW_SHOW);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // 蓝图 §6: COM 初始化（供 LnkResolver 等使用）
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        std::fprintf(stderr, "[main] CoInitializeEx failed: 0x%lx\n",
                     static_cast<unsigned long>(hr));
        return 1;
    }

    {
        geofinder::Application app;
        int ret = app.run();
        // app 析构在 CoUninitialize 之前，确保 COM 资源已释放
        if (ret != 0) {
            CoUninitialize();
            if (hMutex) CloseHandle(hMutex);
            return ret;
        }
    }

    CoUninitialize();
    if (hMutex) CloseHandle(hMutex);
    return 0;
}
