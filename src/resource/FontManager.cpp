// 参考蓝图 §5.5 — FontManager 实现
//
// FreeType 字形加载 → R8 图集放置 → 字形缓存 → 排版顶点生成。
// 图集管理采用 shelf packing，最多 8 张 2048×2048 图集。
//
// TODO(Sprint3): 图集扩容策略（8 张满后 2048→4096 重建）
// TODO(Sprint3): 多图集 TextRun 拆分（当前假设所有字形落在同一张图集）

#include "resource/FontManager.hpp"
#include "render/IRenderBackend.hpp"
#include "resource/ResourceManager.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cwchar>

// Windows API for wstring → UTF-8 conversion
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "utils/StringUtils.hpp"

namespace geofinder {

// ── 构造 / 析构 ───────────────────────────────────────────────────

FontManager::FontManager(ResourceManager* resMgr, IRenderBackend* backend)
    : m_resMgr(resMgr)
    , m_backend(backend)
{
    // 参考蓝图 §5.5 — 初始化 FreeType 库
    if (FT_Init_FreeType(reinterpret_cast<FT_Library*>(&m_ft)) != 0) {
        std::fprintf(stderr, "[FontManager] FT_Init_FreeType failed\n");
        m_ft = nullptr;
    }
}

FontManager::~FontManager()
{
    // 参考蓝图 §5.5 — 清理 FreeType + 图集
    if (m_face) {
        FT_Done_Face(reinterpret_cast<FT_Face>(m_face));
        m_face = nullptr;
    }
    if (m_ft) {
        FT_Done_FreeType(reinterpret_cast<FT_Library>(m_ft));
        m_ft = nullptr;
    }
    // 注销图集纹理（持久纹理，需显式注销）
    for (auto& atlas : m_atlases) {
        if (atlas.handle != kInvalidHandle && m_resMgr) {
            m_resMgr->unregisterTexture(atlas.handle);
        }
    }
    m_atlases.clear();
    m_glyphCache.clear();
}

// ── 字体加载 ──────────────────────────────────────────────────────

bool FontManager::loadFont(const std::wstring& path, float pixelSize)
{
    // 参考蓝图 §5.5 — 加载字体文件并设置基准字号
    if (!m_ft) {
        std::fprintf(stderr, "[FontManager] FreeType not initialized\n");
        return false;
    }

    std::string utf8Path = StringUtils::wideToUtf8(path);
    if (utf8Path.empty()) {
        std::fprintf(stderr, "[FontManager] Path conversion failed\n");
        return false;
    }

    FT_Face face = nullptr;
    FT_Error err = FT_New_Face(reinterpret_cast<FT_Library>(m_ft),
                               utf8Path.c_str(), 0, &face);
    if (err != 0) {
        std::fprintf(stderr, "[FontManager] FT_New_Face failed: %s (err=%d)\n",
                     utf8Path.c_str(), static_cast<int>(err));
        return false;
    }

    // 如果之前已加载字体，先释放
    if (m_face) {
        FT_Done_Face(reinterpret_cast<FT_Face>(m_face));
    }
    m_face = reinterpret_cast<FT_FaceRec_*>(face);

    // 设置字号
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelSize));

    std::printf("[FontManager] Font loaded: %s\n", utf8Path.c_str());
    return true;
}

// ── 图集管理（shelf packing） ─────────────────────────────────────

FontManager::Atlas& FontManager::getOrCreateAtlas(int glyphW, int glyphH)
{
    // 参考蓝图 §5.5 — Shelf packing 图集分配
    //
    // 1. 尝试放入当前行
    // 2. 放不下则换行
    // 3. 图集也放不下则创建新图集

    // 先尝试已有图集
    for (auto& atlas : m_atlases) {
        // 检查当前行能否容纳
        if (atlas.usedWidth + glyphW <= ATLAS_SIZE &&
            atlas.usedHeight + glyphH <= ATLAS_SIZE) {
            // 当前行放得下（高度检查：当前行起点的剩余高度）
            int rowTop = atlas.usedHeight;
            if (rowTop + glyphH <= ATLAS_SIZE) {
                return atlas;  // 放在当前行当前位置
            }
        }

        // 尝试换到下一行
        int nextRowTop = atlas.usedHeight + atlas.rowHeight;
        if (nextRowTop + glyphH <= ATLAS_SIZE && glyphW <= ATLAS_SIZE) {
            // 换行
            atlas.usedWidth = 0;
            atlas.usedHeight = nextRowTop;
            atlas.rowHeight = 0;
            return atlas;
        }
    }

    // 所有已有图集都放不下 → 创建新图集
    if (static_cast<int>(m_atlases.size()) < MAX_ATLASES) {
        // 使用持久纹理（非临时纹理），因为图集需要跨帧保留。
        // 零初始化避免 GL_LINEAR 采样时读到未初始化 GPU 内存。
        TextureDesc desc;
        desc.width = ATLAS_SIZE;
        desc.height = ATLAS_SIZE;
        desc.format = TextureDesc::Format::R8;
        desc.pixels.resize(static_cast<size_t>(ATLAS_SIZE * ATLAS_SIZE), 0);
        ResourceHandle handle = m_resMgr->registerTexture(desc);

        Atlas newAtlas;
        newAtlas.handle = handle;
        m_atlases.push_back(newAtlas);

        std::printf("[FontManager] Created atlas #%zu (handle=%llu)\n",
                    m_atlases.size(),
                    static_cast<unsigned long long>(handle));
        return m_atlases.back();
    }

    // 8 张图集全满 — 使用最后一张，字形会被截断
    // TODO(Sprint3): 实现扩容策略（2048 → 4096 重建所有图集）
    std::fprintf(stderr, "[FontManager] All %d atlases full!\n", MAX_ATLASES);
    return m_atlases.back();
}

// ── 字形获取（带缓存） ────────────────────────────────────────────

const GlyphInfo& FontManager::getGlyph(uint32_t codepoint, float fontSize)
{
    // 参考蓝图 §5.5 — 字形缓存键
    // bucket = (int)(fontSize / 2) * 2，2px 粒度
    int bucket = (static_cast<int>(fontSize) / 2) * 2;
    uint64_t key = (static_cast<uint64_t>(codepoint) << 16)
                   | static_cast<uint64_t>(bucket);

    // 缓存命中
    auto it = m_glyphCache.find(key);
    if (it != m_glyphCache.end()) {
        return it->second;
    }

    // 缓存未命中 → 加载 + 光栅化 + 放入图集
    GlyphInfo info;
    info.atlasHandle = kInvalidHandle;

    if (!m_face) {
        // 无字体加载 — 返回空的 GlyphInfo，后续跳过渲染
        m_glyphCache[key] = info;
        return m_glyphCache[key];
    }

    FT_Face face = reinterpret_cast<FT_Face>(m_face);

    // 设置字号（FreeType 使用 26.6 固定点数，此处用像素大小）
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(fontSize));

    // 加载字形并强制光栅化为 8-bit 灰度
    // FT_LOAD_NO_BITMAP 跳过字体嵌入位图（某些 CJK 字体在不同字号下
    // 返回不同 pixel_mode，统一使用轮廓缩放保证一致格式）
    // FT_LOAD_TARGET_NORMAL 确保 grayscale 抗锯齿
    FT_Error err = FT_Load_Char(face, static_cast<FT_ULong>(codepoint),
                                FT_LOAD_RENDER | FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_NORMAL);
    if (err != 0) {
        // 字形不存在（如缺失字符）— 缓存空信息，避免反复加载
        m_glyphCache[key] = info;
        return m_glyphCache[key];
    }

    FT_GlyphSlot slot = face->glyph;
    FT_Bitmap& bitmap = slot->bitmap;

    int glyphW = static_cast<int>(bitmap.width);
    int glyphH = static_cast<int>(bitmap.rows);

    if (glyphW <= 0 || glyphH <= 0) {
        // 空白字符（如空格）— 只有 advance
        info.advance = static_cast<float>(slot->advance.x) / 64.0f;
        m_glyphCache[key] = info;
        return m_glyphCache[key];
    }

    // 在图集中分配空间
    Atlas& atlas = getOrCreateAtlas(glyphW, glyphH);
    info.atlasHandle = atlas.handle;

    int dstX = atlas.usedWidth;
    int dstY = atlas.usedHeight;

    // 将字形像素写入图集
    // 注意：GL_UNPACK_ALIGNMENT=1 已在 updateTextureRegion 中设置

    // FreeType bitmap.pitch 有符号：正=自上而下，负=自下而上。
    // 自下而上时 buffer 指向末行，需翻转行序。
    {
        int pitch = bitmap.pitch;
        if (pitch < 0) pitch = -pitch;

        std::vector<uint8_t> tight(static_cast<size_t>(glyphW * glyphH));
        for (int row = 0; row < glyphH; ++row) {
            int srcRow = (bitmap.pitch < 0) ? (glyphH - 1 - row) : row;
            std::memcpy(tight.data() + static_cast<size_t>(row * glyphW),
                        bitmap.buffer + srcRow * bitmap.pitch,
                        static_cast<size_t>(glyphW));
        }
        m_backend->updateTextureRegion(atlas.handle,
                                       dstX, dstY, glyphW, glyphH,
                                       tight.data());
    }

    // 计算 UV（图集中像素范围，归一化到 [0,1]）
    // GL_NEAREST + CLAMP_TO_EDGE 直接映射像素，无需半像素偏移
    float atlasSizeF = static_cast<float>(ATLAS_SIZE);
    info.uvMin = glm::vec2(static_cast<float>(dstX) / atlasSizeF,
                           static_cast<float>(dstY) / atlasSizeF);
    info.uvMax = glm::vec2(static_cast<float>(dstX + glyphW) / atlasSizeF,
                           static_cast<float>(dstY + glyphH) / atlasSizeF);

    // 字形度量（26.6 固定点 → 像素浮点）
    info.size = glm::vec2(static_cast<float>(glyphW),
                          static_cast<float>(glyphH));
    info.bearing = glm::vec2(
        static_cast<float>(slot->bitmap_left),
        static_cast<float>(slot->bitmap_top));
    info.advance = static_cast<float>(slot->advance.x) / 64.0f;

    // 更新图集占用信息
    atlas.usedWidth += glyphW;
    atlas.rowHeight = std::max(atlas.rowHeight, glyphH);

    // 放入缓存
    m_glyphCache[key] = info;
    return m_glyphCache[key];
}

// ── 文本排版 ──────────────────────────────────────────────────────

FontManager::TextRun FontManager::createTextRun(
    const std::wstring& text, float fontSize,
    float maxWidth, const glm::vec4& /*color*/)
{
    // 参考蓝图 §5.5 — 创建文本排版结果
    //
    // 逐字符获取 GlyphInfo，计算屏幕位置，生成顶点+索引。
    // 假设所有字形落在同一张图集（2048×2048 通常足够）。
    //
    // TODO(Sprint3): 多图集拆分 — 如果字形落在不同图集，拆为多个 TextRun

    TextRun run;
    run.atlasHandle = kInvalidHandle;

    if (text.empty() || !m_face) return run;

    float penX = 0.0f;
    float lineHeight = fontSize * 1.2f;  // 行高 = 字号 × 1.2
    float penY = fontSize;               // 基线位置（从顶部算起）
    float maxPenX = 0.0f;

    for (wchar_t ch : text) {
        uint32_t cp = static_cast<uint32_t>(ch);
        const GlyphInfo& glyph = getGlyph(cp, fontSize);

        // 跳过无效字形
        if (glyph.advance <= 0.0f && glyph.size.x <= 0.0f) {
            continue;
        }

        // 换行检查
        if (penX + glyph.advance > maxWidth && penX > 0.0f) {
            penX = 0.0f;
            penY += lineHeight;
        }

        // 如果这是第一个有效字形，记录其图集句柄
        if (run.atlasHandle == kInvalidHandle && glyph.atlasHandle != kInvalidHandle) {
            run.atlasHandle = glyph.atlasHandle;
        }

        // 空格等无位图字形：仅推进 penX
        if (glyph.atlasHandle == kInvalidHandle) {
            penX += glyph.advance;
            continue;
        }

        // 计算屏幕四角
        float x0 = penX + glyph.bearing.x;
        float y0 = penY - glyph.bearing.y;                      // 顶部
        float x1 = x0 + glyph.size.x;
        float y1 = y0 + glyph.size.y;                           // 底部

        // 4 个顶点（pos + UV）
        uint32_t baseIdx = static_cast<uint32_t>(run.vertices.size());

        run.vertices.push_back({glm::vec2(x0, y0), glyph.uvMin});                        // 左上
        run.vertices.push_back({glm::vec2(x1, y0), glm::vec2(glyph.uvMax.x, glyph.uvMin.y)}); // 右上
        run.vertices.push_back({glm::vec2(x0, y1), glm::vec2(glyph.uvMin.x, glyph.uvMax.y)}); // 左下
        run.vertices.push_back({glm::vec2(x1, y1), glyph.uvMax});                        // 右下

        // 6 个索引（两个三角形）
        run.indices.push_back(baseIdx + 0);
        run.indices.push_back(baseIdx + 1);
        run.indices.push_back(baseIdx + 2);
        run.indices.push_back(baseIdx + 1);
        run.indices.push_back(baseIdx + 3);
        run.indices.push_back(baseIdx + 2);

        penX += glyph.advance;
        maxPenX = std::max(maxPenX, penX);
    }

    run.totalWidth = maxPenX;
    run.totalHeight = penY + lineHeight * 0.5f;  // 含下行空间

    return run;
}

} // namespace geofinder
