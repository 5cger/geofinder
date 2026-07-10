// 参考蓝图 §5.22 — LnkResolver 快捷方式解析
//
// 通过 IShellLink COM 接口解析 .lnk 文件：
// 提取目标路径、参数、工作目录、图标位置、UWP AppUserModelId。
// COM 初始化由调用者（main.cpp）统一完成。
#pragma once

#include <string>
#include <windows.h>
#include <shlobj.h>

namespace geofinder {

struct LnkInfo {
    std::wstring targetPath;
    std::wstring arguments;
    std::wstring workingDir;
    std::wstring iconPath;
    int iconIndex = 0;
    std::wstring appUserModelId;  // UWP 应用的 AUMID（非空 = UWP）
    bool isUWP = false;
};

class LnkResolver {
public:
    LnkResolver();
    ~LnkResolver();

    /// 解析 .lnk 快捷方式。调用前需保证 COM 已初始化
    /// （CoInitializeEx(COINIT_APARTMENTTHREADED)）。
    /// @return 解析成功返回 true
    bool resolve(const std::wstring& lnkPath, LnkInfo& outInfo);

private:
    /// 从 IShellLink 的 IPropertyStore 提取 AppUserModelId
    bool extractAumId(struct IShellLinkW* shellLink,
                      std::wstring& outAumId);
};

} // namespace geofinder
