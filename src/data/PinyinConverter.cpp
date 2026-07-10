// 参考蓝图 §5.19 — PinyinConverter 实现
//
// 封装 mini-pinyin C 库，提供 C++ 接口。
// toFirstLetters: 中文 → 拼音首字母串
// matchPinyin: 用户输入字母 → 拼音首字母逐组匹配（静态方法）

#include "data/PinyinConverter.hpp"

// mini-pinyin C API
extern "C" {
#include "pinyin.h"
}

#include <cstdio>
#include <sstream>
#include <algorithm>

namespace geofinder {

// ── 构造 / 析构 ─────────────────────────────────────────────────────

PinyinConverter::PinyinConverter() = default;

PinyinConverter::~PinyinConverter() = default;

// ── loadDictionary ───────────────────────────────────────────────────
// mini-pinyin 的字典数据内嵌在 pinyin.c 中，无需外部文件。
// 保留此接口以兼容蓝图，未来可切换为外部字典。

bool PinyinConverter::loadDictionary(const std::string& /*dictPath*/) {
    // mini-pinyin 内嵌字典，无需外部加载
    m_loaded = true;
    return true;
}

// ── toFirstLetters ───────────────────────────────────────────────────

std::string PinyinConverter::toFirstLetters(const std::wstring& text) const {
    // 参考蓝图 §5.19 — 中文 → 拼音首字母串
    //
    // 每个汉字映射到首字母（多音字用逗号分隔），
    // 非汉字字符（ASCII、数字等）原样保留为小写。

    if (!m_loaded) return {};

    // 需要 mini-pinyin 字典句柄（查询用，数据内嵌）
    PinyinDict* dict = pinyin_dict_new();
    if (!dict) return {};

    std::ostringstream oss;

    for (wchar_t wch : text) {
        uint32_t cp = static_cast<uint32_t>(wch);

        // ASCII 字母：直接保留小写
        if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) {
            if (oss.tellp() > 0) oss << ',';
            oss << (char)tolower(cp);
            continue;
        }
        // ASCII 数字：保留
        if (cp >= '0' && cp <= '9') {
            if (oss.tellp() > 0) oss << ',';
            oss << (char)cp;
            continue;
        }

        // 查 mini-pinyin 字典
        char buf[16] = {};
        int len = pinyin_first_letter(dict, cp, buf, sizeof(buf));
        if (len > 0) {
            if (oss.tellp() > 0) oss << ',';
            oss << buf;
        }
        // 非汉字且无映射 → 跳过（不在拼音中贡献字母）
    }

    pinyin_dict_free(dict);
    return oss.str();
}

// ── matchPinyin（静态） ──────────────────────────────────────────────
//
// 蓝图 §5.19 逐组匹配算法：
//   1. 将 pinyinStr 按逗号分割为组
//   2. 每组含该汉字的所有可能首字母
//   3. 逐组消耗 input 字符，每组最多消耗 1 个

bool PinyinConverter::matchPinyin(const std::string& input,
                                   const std::string& pinyinStr) {
    if (input.empty()) return true;   // 空输入匹配一切
    if (pinyinStr.empty()) return false;  // 空拼音只能匹配空输入

    // Step 1 — 按逗号分割拼音串为组
    std::vector<std::vector<char>> groups;
    std::vector<char> current;
    for (size_t i = 0; i < pinyinStr.size(); ++i) {
        char ch = pinyinStr[i];
        if (ch == ',') {
            if (!current.empty()) {
                groups.push_back(std::move(current));
                current.clear();
            }
        } else if (ch >= 'a' && ch <= 'z') {
            current.push_back(ch);
        }
        // 忽略其他字符（空格等）
    }
    if (!current.empty()) {
        groups.push_back(std::move(current));
    }

    if (groups.empty()) return false;

    // Step 2 — 逐组匹配
    size_t inputIdx = 0;
    for (const auto& group : groups) {
        if (inputIdx >= input.size()) break;

        char target = input[inputIdx];
        // 组中查找该字符
        if (std::find(group.begin(), group.end(), target) != group.end()) {
            inputIdx++;  // 消耗一个 input 字符
        }
        // 否则跳过此组（允许不匹配的组）
    }

    // Step 3 — input 必须完全消耗
    return inputIdx == input.size();
}

} // namespace geofinder
