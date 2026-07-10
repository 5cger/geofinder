#pragma once

// 参考蓝图 §5.5 — FontManager 字体排版
//
// FreeType 集成：加载系统字体，将字形光栅化结果放入 2048×2048 R8 图集，
// 缓存字形度量信息，对外提供 createTextRun 生成可直接用于 TextRunOp 的顶点数据。
//
// 图集管理采用 shelf packing 策略，最多 8 张图集。图集满时不驱逐，而是扩容重建。
// 字形缓存以 (codepoint << 16) | fontSizeBucket 为键，2px 粒度分桶。

#include "render/CommandBuffer.hpp"  // GlyphVertex
#include "render/ResourceHandle.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// FreeType 前向声明（避免头文件泄露）
struct FT_LibraryRec_;
struct FT_FaceRec_;

namespace geofinder {

class ResourceManager;
class IRenderBackend;

// ── 字形信息（蓝图 §5.5） ─────────────────────────────────────────

struct GlyphInfo {
    ResourceHandle atlasHandle;  // 所属图集纹理句柄
    glm::vec2 uvMin;             // 图集中 UV 左下
    glm::vec2 uvMax;             // 图集中 UV 右上
    glm::vec2 size;              // 字形像素尺寸
    glm::vec2 bearing;           // 基线偏移（bearingX, bearingY）
    float advance = 0.0f;        // 字符步进宽度
};

// ── FontManager（蓝图 §5.5） ─────────────────────────────────────

class FontManager {
public:
    /// 文本排版结果。包含单张图集上所有字形的顶点数据，
    /// 可直接构造 TextRunOp。
    struct TextRun {
        ResourceHandle atlasHandle;
        std::vector<GlyphVertex> vertices;
        std::vector<uint32_t> indices;
        float totalWidth = 0.0f;
        float totalHeight = 0.0f;
    };

    /// @param resMgr  用于创建图集纹理（allocateTemporary）
    /// @param backend 用于更新图集纹理（updateTextureRegion → glTexSubImage2D）
    FontManager(ResourceManager* resMgr, IRenderBackend* backend);
    ~FontManager();

    // 禁止拷贝
    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;

    /// 加载字体文件。调用前需确保 FreeType 已初始化。
    /// @param path      字体文件路径（如 C:/Windows/Fonts/msyh.ttc）
    /// @param pixelSize 基准字号（像素），后续 createTextRun 可覆盖
    /// @return 加载成功返回 true
    bool loadFont(const std::wstring& path, float pixelSize);

    /// 创建文本排版结果。返回字形顶点数据，可直接用于 TextRunOp。
    /// @param text     待排版的文本
    /// @param fontSize 字号（像素），2px 粒度分桶
    /// @param maxWidth 最大行宽（像素），超出自动换行
    /// @param color    文本颜色（存入 TextRunOp，此处仅用于 alpha）
    /// @return 排版结果（顶点 + 索引 + 图集句柄）
    TextRun createTextRun(const std::wstring& text, float fontSize,
                          float maxWidth, const glm::vec4& color);

private:
    // ── 图集管理 ─────────────────────────────────────────────

    struct Atlas {
        ResourceHandle handle;
        int usedWidth = 0;   // 当前行已用宽度
        int usedHeight = 0;  // 累计行高（下一行起始 Y）
        int rowHeight = 0;   // 当前行最大字形高度
    };

    static constexpr int ATLAS_SIZE = 2048;
    static constexpr int MAX_ATLASES = 8;

    /// 获取或创建能容纳 glyphW×glyphH 的图集。
    /// 采用 shelf packing：当前行放不下则换行，图集满则新建。
    Atlas& getOrCreateAtlas(int glyphW, int glyphH);

    // ── 字形缓存 ─────────────────────────────────────────────

    /// 获取字形信息（缓存命中直接返回，未命中则加载+放入图集+缓存）。
    /// key = (codepoint << 16) | fontSizeBucket
    /// fontSizeBucket = (int)(fontSize / 2) * 2，2px 粒度
    const GlyphInfo& getGlyph(uint32_t codepoint, float fontSize);

    std::unordered_map<uint64_t, GlyphInfo> m_glyphCache;

    // ── 依赖注入 ─────────────────────────────────────────────

    ResourceManager* m_resMgr;
    IRenderBackend* m_backend;

    // ── FreeType ──────────────────────────────────────────────

    FT_LibraryRec_* m_ft = nullptr;
    FT_FaceRec_* m_face = nullptr;

    // ── 图集列表 ─────────────────────────────────────────────

    std::vector<Atlas> m_atlases;
};

} // namespace geofinder
