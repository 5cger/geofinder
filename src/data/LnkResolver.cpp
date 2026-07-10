// 参考蓝图 §5.22 — LnkResolver 实现
//
// IShellLinkW + IPersistFile 加载 .lnk，
// 通过 IPropertyStore 检测 UWP AppUserModelId。

#include "data/LnkResolver.hpp"

#include <wrl/client.h>
#include <propkey.h>
#include <propvarutil.h>

#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace geofinder {

LnkResolver::LnkResolver() = default;
LnkResolver::~LnkResolver() = default;

bool LnkResolver::resolve(const std::wstring& lnkPath, LnkInfo& outInfo)
{
    // 蓝图 §5.22 — 使用 IShellLinkW + IPersistFile 解析

    ComPtr<IShellLinkW> shellLink;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&shellLink));
    if (FAILED(hr)) {
        std::fprintf(stderr, "[LnkResolver] CoCreateInstance IShellLink failed: 0x%lx\n",
                     static_cast<unsigned long>(hr));
        return false;
    }

    ComPtr<IPersistFile> persistFile;
    hr = shellLink.As(&persistFile);
    if (FAILED(hr)) {
        std::fprintf(stderr, "[LnkResolver] QueryInterface IPersistFile failed\n");
        return false;
    }

    hr = persistFile->Load(lnkPath.c_str(), STGM_READ);
    if (FAILED(hr)) {
        // 不是致命错误 — 某些 .lnk 可能损坏
        return false;
    }

    wchar_t target[MAX_PATH] = {};
    wchar_t args[MAX_PATH] = {};
    wchar_t workDir[MAX_PATH] = {};
    wchar_t iconPath[MAX_PATH] = {};
    int iconIdx = 0;

    shellLink->GetPath(target, MAX_PATH, nullptr, SLGP_RAWPATH);
    shellLink->GetArguments(args, MAX_PATH);
    shellLink->GetWorkingDirectory(workDir, MAX_PATH);
    shellLink->GetIconLocation(iconPath, MAX_PATH, &iconIdx);

    outInfo.targetPath = target;
    outInfo.arguments = args;
    outInfo.workingDir = workDir;
    outInfo.iconPath = iconPath;
    outInfo.iconIndex = iconIdx;

    // 检测 UWP AppUserModelId
    extractAumId(shellLink.Get(), outInfo.appUserModelId);
    outInfo.isUWP = !outInfo.appUserModelId.empty();

    return true;
}

bool LnkResolver::extractAumId(IShellLinkW* shellLink,
                                std::wstring& outAumId)
{
    // 蓝图 §5.22 — 通过 IPropertyStore 获取 PKEY_AppUserModel_ID
    ComPtr<IPropertyStore> propStore;
    HRESULT hr = shellLink->QueryInterface(IID_PPV_ARGS(&propStore));
    if (FAILED(hr)) return false;

    PROPVARIANT pv;
    PropVariantInit(&pv);
    hr = propStore->GetValue(PKEY_AppUserModel_ID, &pv);
    if (SUCCEEDED(hr) && pv.vt == VT_LPWSTR && pv.pwszVal) {
        outAumId = pv.pwszVal;
    }
    PropVariantClear(&pv);
    return !outAumId.empty();
}

} // namespace geofinder
