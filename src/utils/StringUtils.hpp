// 参考蓝图 §5.27 — StringUtils 编码转换
//
// UTF-8 ↔ UTF-16 (wstring) 双向转换，集中管理，避免各模块重复实现。
#pragma once

#include <string>

namespace geofinder {
namespace StringUtils {

/// wstring (UTF-16) → UTF-8
std::string wideToUtf8(const std::wstring& w);

/// UTF-8 → wstring (UTF-16)
std::wstring utf8ToWide(const std::string& s);

} // namespace StringUtils
} // namespace geofinder
