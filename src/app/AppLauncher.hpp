// 参考蓝图 §5.23 — AppLauncher 应用启动
//
// 根据应用类型选择启动方式：
// - CreateProcess：传统 .exe
// - ShellExecuteEx：支持参数、工作目录、管理员权限
// - IApplicationActivationManager：UWP 应用
//
// COM 由 main.cpp 统一初始化，本模块不重复初始化。
#pragma once

#include "data/DataTypes.hpp"
#include <string>

namespace geofinder {

class AppLauncher {
public:
    AppLauncher();
    ~AppLauncher();

    /// 启动应用。调用前需保证 COM 已初始化。
    /// @return 启动成功返回 true
    bool launch(const SearchResultEntry& entry);

private:
    bool launchExe(const std::wstring& path,
                   const std::wstring& args,
                   const std::wstring& workDir);
    bool launchUWP(const std::wstring& appUserModelId);
};

} // namespace geofinder
