// 参考蓝图 §5.19 — PinyinConverter 拼音转换
//
// 纯实例类（非单例），由 Application 管理生命周期。
// 依赖 vendored mini-pinyin 字典数据（third_party/mini-pinyin/）。
// 支持多音字：每个汉字的所有读音首字母用逗号分隔。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace geofinder {

class PinyinConverter {
public:
    PinyinConverter();
    ~PinyinConverter();

    /// 加载拼音字典（从 mini-pinyin 数据文件）。
    /// @param dictPath 字典文件路径（如 "third_party/mini-pinyin/pinyin.txt"）
    /// @return 加载成功返回 true
    bool loadDictionary(const std::string& dictPath);

    /// 将中文字符串转为拼音首字母串。
    /// 多音字处理：每个汉字的所有读音首字母用逗号分隔。
    /// 非汉字字符原样保留（小写）。
    /// 例："长大" → "c,z,d"（"长" → "c,z"，"大" → "d"）
    /// 例："微信" → "w,x"（"微" → "w"，"信" → "x"）
    std::string toFirstLetters(const std::wstring& text) const;

    /// 用户输入匹配检查（蓝图 §5.19 逐组匹配算法）。
    ///
    /// @param input     用户输入的字母串（如 "wx"、"zd"）
    /// @param pinyinStr toFirstLetters 的返回值（如 "w,x"、"c,z,d"）
    /// @return 匹配成功返回 true
    ///
    /// 匹配规则：
    ///   1. 将 pinyinStr 按逗号分割为每组首字母列表
    ///   2. 用户输入逐字符按组匹配，每组最多消耗 1 个字符
    ///   3. 组可被跳过（不匹配），所有 input 字符必须被消耗
    ///
    /// 例：input="wx", pinyinStr="w,x"
    ///   groups=[["w"],["x"]] → "w"∈["w"] ✓, "x"∈["x"] ✓ → true
    /// 例：input="zd", pinyinStr="c,z,d"
    ///   groups=[["c","z"],["d"]] → "z"∈["c","z"] ✓, "d"∈["d"] ✓ → true
    static bool matchPinyin(const std::string& input,
                            const std::string& pinyinStr);

private:
    // codepoint → 所有可能的拼音首字母（char 列表）
    std::unordered_map<uint32_t, std::vector<char>> m_dict;
    bool m_loaded = false;
};

} // namespace geofinder
