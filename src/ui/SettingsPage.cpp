// SettingsPage — 预计算布局 + 高亮实时跟随（参照 ResultList）
#include "ui/SettingsPage.hpp"
#include "ui/InputField.hpp"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <algorithm>
#include <cmath>

namespace geofinder {

static const glm::vec4 kAccent(0.3f,0.6f,1,1), kBg(0.08f,0.08f,0.10f,1), kCard(0.12f,0.12f,0.14f,1);
static const glm::vec4 kDim(0.45f,0.45f,0.50f,1), kBright(1,1,1,1);

static void rect(CommandBuffer& cb, glm::vec2 p, glm::vec2 s, glm::vec4 c) {
    RenderCommand cmd; RectOp r; r.pos=p; r.size=s; r.color=c; cmd.op=r; cb.add(cmd);
}

SettingsPage::SettingsPage(FontManager* fm, ConfigManager* cfg): m_fm(fm), m_cfg(cfg) {
    m_size = glm::vec2(780,660); refresh();
}

void SettingsPage::refresh() {
    if (m_cfg) m_dirs = m_cfg->getSearchDirs();
    m_as = m_cfg ? m_cfg->getAnimSpeed() : 10;
    m_cs = m_cfg ? m_cfg->getCursorSpeed() : 14;
}

void SettingsPage::setScanProgress(int n, const std::wstring& d){ m_scanProg=n; m_scanDir=d; }
void SettingsPage::setScanComplete(int t){ m_scanning=false; m_scanTotal=t; }
void SettingsPage::setScanning(bool s){ m_scanning=s; if(s){m_scanProg=0;m_scanTotal=0;} }

void SettingsPage::renderText(CommandBuffer& cb, const std::wstring& t,
                               float x, float y, float sz, const glm::vec4& c) {
    if (!m_fm||t.empty()) return;
    auto r = m_fm->createTextRun(t, sz, 9999, c);
    if (r.atlasHandle==kInvalidHandle||r.vertices.empty()) return;
    RenderCommand cmd; TextRunOp op; op.textureHandle=r.atlasHandle; op.vertices.resize(r.vertices.size());
    for (size_t i=0;i<r.vertices.size();++i) {
        op.vertices[i].pos=glm::vec2(r.vertices[i].pos.x+x,r.vertices[i].pos.y+y);
        op.vertices[i].uv=r.vertices[i].uv;
    }
    op.indices=std::move(r.indices); op.color=c; op.opacity=1; cmd.op=op; cb.add(cmd);
}

// ── 布局重算 ──────────────────────────────────────────────────

void SettingsPage::recalcLayout() {
    float y = m_pos.y + 56;  // "设 置" 标题下方
    for (int i = 0; i < kCards; ++i) {
        m_cardY[i] = y;
        y += kCardH + kGap;
        if (m_inSection && i == m_focusIdx)
            y += m_expandH[i] + 8;
    }
    setFocus(m_focusIdx);
}

void SettingsPage::setFocus(int idx) {
    m_focusIdx = idx;
    m_hlTgt = m_cardY[idx];
    m_hlAlpha = 0.5f;
    m_hlScaleTgt = 1.06f;
}

// ── 动画 ──────────────────────────────────────────────────────

void SettingsPage::update(float delta) {
    float dt = std::min(delta, 0.1f);
    m_hlY += (m_hlTgt - m_hlY) * kSpd * dt;
    m_hlAlpha += (1.0f - m_hlAlpha) * kSpd * dt;
    m_hlScale += (m_hlScaleTgt - m_hlScale) * kSpd * dt;
    for (int i = 0; i < 3; ++i) {
        m_expandH[i] += (m_expandT[i] - m_expandH[i]) * kSpd * dt;
        if (!m_inSection && m_expandT[i]==0 && m_expandH[i] < 0.5f) m_expandH[i]=0;
    }
    // 关闭动画完成
    if (m_inSection && m_expandT[m_focusIdx]==0 && m_expandH[m_focusIdx]<0.5f) {
        m_inSection = false; m_hlAlpha = 0.5f; recalcLayout();
    }
}

bool SettingsPage::isAnimating() const {
    if (std::abs(m_hlY-m_hlTgt)>0.5f||std::abs(m_hlAlpha-1)>0.01f||std::abs(m_hlScale-m_hlScaleTgt)>0.001f) return true;
    for (int i=0;i<3;++i) if (std::abs(m_expandH[i]-m_expandT[i])>0.5f) return true;
    return false;
}

// ── 绘制 ──────────────────────────────────────────────────────

void SettingsPage::paint(CommandBuffer& cb) {
    if (!m_visible) return;
    float px = m_pos.x + kPadX, w = m_size.x - 2*kPadX;
    rect(cb, m_pos, m_size, kBg);
    renderText(cb, L"\u8BBE \u7F6E", px, m_pos.y+16, 20, kAccent);

    recalcLayout();  // 每帧重算卡片 Y（展开动画会改变位置）

    for (int i = 0; i < kCards; ++i) {
        bool focused = (i == m_focusIdx);
        bool expanded = (m_inSection && i == m_focusIdx) || (!m_inSection && m_expandH[i] > 4);

        // 卡片背景
        rect(cb, glm::vec2(px, m_cardY[i]), glm::vec2(w, kCardH), kCard);

        // 焦点高亮（在卡片之上，参照 ResultList）
        if (focused) {
            float ha = 0.4f * m_hlAlpha;
            rect(cb, glm::vec2(px+4, m_hlY+1), glm::vec2(w-8, kCardH-2), glm::vec4(0.25f,0.45f,0.85f,ha));
            rect(cb, glm::vec2(px, m_hlY), glm::vec2(3, kCardH), glm::vec4(0.3f,0.6f,1,m_hlAlpha));
        }

        // 摘要（右侧）
        std::wstring sum;
        if (i == 0) sum = std::to_wstring(m_dirs.size()) + L" 个目录";
        else if (i == 1) sum = m_scanning ? L"扫描中..." : (m_scanTotal > 0 ? std::to_wstring(m_scanTotal) + L" 文件" : L"就绪");
        else { wchar_t b[48]; _snwprintf(b, 48, L"高亮 %.0f  光标 %.0f", m_as, m_cs); sum = b; }
        // 摘要放在卡片右侧
        float sumW = sum.size() * 8.0f;  // 粗略宽度估算
        renderText(cb, sum, px + w - sumW - 40, m_cardY[i] + 30, 13, kDim);

        // 类型标签（放在卡片左上角）
        const wchar_t* labels[3] = {L"目录", L"状态", L"动画"};
        renderText(cb, labels[i], px + 12, m_cardY[i] + 30, 13, glm::vec4(0.3f, 0.6f, 1.0f, 1));
        // 标题
        const wchar_t* titles[3] = {L"扫描目录", L"扫描状态", L"动画速度"};
        renderText(cb, titles[i], px + 60, m_cardY[i] + 8, 17, focused ? kBright : glm::vec4(0.7f, 0.7f, 0.75f, 1));

        // 展开箭头（选中时）

        // 展开面板
        if (expanded && m_expandH[i] > 4) {
            float ey = m_cardY[i] + kCardH + 4, eh = m_expandH[i];
            rect(cb, glm::vec2(px+8, ey), glm::vec2(w-16, eh), glm::vec4(0.09f,0.09f,0.11f,0.95f));
            float ix = px+16, iy = ey+8;
            if (i == 0) {
                for (size_t j=0;j<m_dirs.size()&&j<5;++j) {
                    if ((int)j==m_selDir) rect(cb,glm::vec2(ix-4,iy-2),glm::vec2(w-40,24),glm::vec4(0.2f,0.4f,0.8f,0.3f));
                    std::wstring p(m_dirs[j].begin(),m_dirs[j].end());
                    if (p.size()>50) p=p.substr(0,25)+L"..."+p.substr(p.size()-22);
                    renderText(cb,p,ix,iy,13,(int)j==m_selDir?kBright:glm::vec4(0.65f,0.65f,0.7f,1));
                    iy+=25;
                }
                renderText(cb,L"Ctrl+R \u91CD\u65B0\u626B\u63CF  Del \u5220\u9664",ix,iy+2,12,glm::vec4(0.35f,0.4f,0.5f,1));
                // 输入框放置在目录列表下方
                iy += 24;
                if (m_inputField) {
                    m_inputField->layout(
                        glm::vec2(m_pos.x + kPadX + 16, m_cardY[i] + kCardH + 4 + iy),
                        glm::vec2(w - 32, 36));
                    m_inputField->setVisible(true);
                    m_inputField->setPlaceholder(L"\u8F93\u5165\u8DEF\u5F84\u6309 Enter \u6DFB\u52A0...");
                }
            } else if (i == 1) {
                if (m_scanning) {
                    float by=iy+12,bh=6,pc=std::min(1.0f,(float)m_scanProg/20000.0f);
                    rect(cb,glm::vec2(ix,by),glm::vec2(w-32,bh),glm::vec4(0.15f,0.15f,0.18f,1));
                    if (pc>0) rect(cb,glm::vec2(ix,by),glm::vec2((w-32)*pc,bh),glm::vec4(0.2f,0.55f,0.9f,1));
                    wchar_t t[64]; _snwprintf(t,64,L"\u626B\u63CF\u4E2D %d \u6587\u4EF6",m_scanProg);
                    renderText(cb,t,ix,iy,13,glm::vec4(0.7f,0.8f,0.95f,1));
                } else if (m_scanTotal>0) {
                    wchar_t t[64]; _snwprintf(t,64,L"\u5DF2\u5B8C\u6210 %d \u4E2A\u6587\u4EF6",m_scanTotal);
                    renderText(cb,t,ix,iy+4,14,glm::vec4(0.3f,0.8f,0.4f,1));
                } else renderText(cb,L"\u672A\u5F00\u59CB\u626B\u63CF",ix,iy+4,14,kDim);
                renderText(cb,L"Enter \u5F00\u59CB\u626B\u63CF",ix,iy+28,12,glm::vec4(0.35f,0.4f,0.5f,1));
            } else if (i == 2) {
                bool hf=(m_subFocus==0);
                renderText(cb,L"\u9AD8\u4EAE/\u6EDA\u52A8\u901F\u5EA6",ix,iy,14,glm::vec4(0.6f,0.6f,0.7f,1));iy+=22;
                if (hf) rect(cb,glm::vec2(ix-4,iy-2),glm::vec2(140,26),glm::vec4(0.2f,0.4f,0.8f,0.25f));
                { wchar_t b[32];_snwprintf(b,32,L"  %.0f",m_as); renderText(cb,b,ix,iy,20,hf?kBright:kDim); }
                iy+=28;
                renderText(cb,L"\u5149\u6807\u6ED1\u52A8\u901F\u5EA6",ix,iy,14,glm::vec4(0.6f,0.6f,0.7f,1));iy+=22;
                if (!hf) rect(cb,glm::vec2(ix-4,iy-2),glm::vec2(140,26),glm::vec4(0.2f,0.4f,0.8f,0.25f));
                { wchar_t b[32];_snwprintf(b,32,L"  %.0f",m_cs); renderText(cb,b,ix,iy,20,hf?kDim:kBright); }
            }
        }
    }
    renderText(cb, m_inSection?L"Esc \u8FD4\u56DE":L"\u2191\u2193 \u9009\u62E9  Enter \u8FDB\u5165  Esc \u8FD4\u56DE",
               px, m_pos.y+m_size.y-22, 12, glm::vec4(0.3f,0.35f,0.4f,1));
}

// ── 键盘 ──────────────────────────────────────────────────────

bool SettingsPage::onKeyEvent(const KeyEvent& evt) {
    if (evt.action!=GLFW_PRESS && evt.action!=GLFW_REPEAT) return false;
    if (m_inSection) {
        switch (m_focusIdx) {
        case 0:
            if (evt.key==GLFW_KEY_UP&&m_selDir>0){m_selDir--;return true;}
            if (evt.key==GLFW_KEY_DOWN&&m_selDir<(int)m_dirs.size()-1){m_selDir++;return true;}
            if (evt.key==GLFW_KEY_DELETE&&m_selDir>=0&&m_selDir<(int)m_dirs.size()&&m_cfg){
                m_cfg->removeSearchDir(m_dirs[m_selDir]); m_cfg->save(); refresh(); return true;}
            if (evt.key==GLFW_KEY_R&&(evt.mods&GLFW_MOD_CONTROL)){if(m_onRescan)m_onRescan();return true;}
            break;
        case 1:
            if (evt.key==GLFW_KEY_ENTER){if(m_onRescan)m_onRescan();return true;} break;
        case 2:
            if (evt.key==GLFW_KEY_UP||evt.key==GLFW_KEY_DOWN){m_subFocus=(m_subFocus+1)%2;return true;}
            if (evt.key==GLFW_KEY_LEFT){
                if (m_subFocus==0){m_as=std::max(1.0f,m_as-1);if(m_cfg){m_cfg->setAnimSpeed(m_as);m_cfg->save();}if(m_onAnimSpeed)m_onAnimSpeed(m_as);}
                else {m_cs=std::max(1.0f,m_cs-1);if(m_cfg){m_cfg->setCursorSpeed(m_cs);m_cfg->save();}}
                return true;
            }
            if (evt.key==GLFW_KEY_RIGHT){
                if (m_subFocus==0){m_as=std::min(30.0f,m_as+1);if(m_cfg){m_cfg->setAnimSpeed(m_as);m_cfg->save();}if(m_onAnimSpeed)m_onAnimSpeed(m_as);}
                else {m_cs=std::min(30.0f,m_cs+1);if(m_cfg){m_cfg->setCursorSpeed(m_cs);m_cfg->save();}}
                return true;
            }
            break;
        }
        if (evt.key==GLFW_KEY_ESCAPE) { m_expandT[m_focusIdx]=0; return true; }
        return true;
    }
    switch (evt.key) {
    case GLFW_KEY_UP:   if(m_focusIdx>0) setFocus(m_focusIdx-1); return true;
    case GLFW_KEY_DOWN: if(m_focusIdx<kCards-1) setFocus(m_focusIdx+1); return true;
    case GLFW_KEY_ENTER:
        m_inSection = true; m_selDir=0; m_subFocus=0;
        m_expandT[m_focusIdx] = kExpandH;
        m_hlAlpha = 0.5f;
        if (m_focusIdx==1 && m_onRescan) m_onRescan();
        return true;
    case GLFW_KEY_ESCAPE: if(m_onBack) m_onBack(); return true;
    default: return false;
    }
}

} // namespace geofinder
