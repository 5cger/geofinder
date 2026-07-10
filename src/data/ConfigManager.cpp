// ConfigManager — JSON 配置（含动画设置）
#include "data/ConfigManager.hpp"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <fstream>

using json = nlohmann::json;

namespace geofinder {

ConfigManager::ConfigManager(const std::string& configPath) : m_path(configPath) {}

bool ConfigManager::load()
{
    std::ifstream ifs(m_path);
    if (!ifs.is_open()) return true;  // 首次运行

    try {
        json j = json::parse(ifs);
        if (j.contains("searchDirs") && j["searchDirs"].is_array())
            m_searchDirs = j["searchDirs"].get<std::vector<std::string>>();
        if (j.contains("animSpeed")) m_animSpeed = j["animSpeed"].get<float>();
        if (j.contains("cursorSpeed")) m_cursorSpeed = j["cursorSpeed"].get<float>();
        std::printf("[Config] loaded: %zu dirs, anim=%.1f cursor=%.1f\n",
                    m_searchDirs.size(), m_animSpeed, m_cursorSpeed);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[Config] parse error: %s\n", e.what());
        return false;
    }
    return true;
}

bool ConfigManager::save()
{
    try {
        json j;
        j["searchDirs"] = m_searchDirs;
        j["animSpeed"] = m_animSpeed;
        j["cursorSpeed"] = m_cursorSpeed;
        std::ofstream ofs(m_path);
        if (!ofs.is_open()) return false;
        ofs << j.dump(2) << std::endl;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[Config] save error: %s\n", e.what());
        return false;
    }
    return true;
}

void ConfigManager::addSearchDir(const std::string& dir) {
    for (const auto& d : m_searchDirs) if (d == dir) return;
    m_searchDirs.push_back(dir);
}
void ConfigManager::removeSearchDir(const std::string& dir) {
    m_searchDirs.erase(std::remove(m_searchDirs.begin(), m_searchDirs.end(), dir), m_searchDirs.end());
}
void ConfigManager::setSearchDirs(const std::vector<std::string>& dirs) { m_searchDirs = dirs; }
void ConfigManager::ensureDefaults(const std::vector<std::string>& defaults) {
    if (m_searchDirs.empty()) { m_searchDirs = defaults; save(); }
}

} // namespace geofinder
