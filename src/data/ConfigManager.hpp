// 参考蓝图 §5.18 — ConfigManager 配置管理（简化版）
//
// JSON 配置文件存储在 exe 同目录。
// 管理扫描目录列表，支持添加/删除/列出。
#pragma once

#include <string>
#include <vector>

namespace geofinder {

class ConfigManager {
public:
    explicit ConfigManager(const std::string& configPath);

    /// 加载配置（不存在则填充默认扫描目录）
    bool load();

    /// 首次运行或目录为空时，填充默认目录
    void ensureDefaults(const std::vector<std::string>& defaults);

    /// 保存配置
    bool save();

    // ── 扫描目录 ────────────────────────────────────────────
    const std::vector<std::string>& getSearchDirs() const { return m_searchDirs; }
    void addSearchDir(const std::string& dir);
    void removeSearchDir(const std::string& dir);
    void setSearchDirs(const std::vector<std::string>& dirs);

    // ── 动画设置 ────────────────────────────────────────────
    float getAnimSpeed() const { return m_animSpeed; }
    void setAnimSpeed(float v) { m_animSpeed = v; }
    float getCursorSpeed() const { return m_cursorSpeed; }
    void setCursorSpeed(float v) { m_cursorSpeed = v; }

private:
    std::string m_path;
    std::vector<std::string> m_searchDirs;
    float m_animSpeed = 10.0f;
    float m_cursorSpeed = 14.0f;
};

} // namespace geofinder
