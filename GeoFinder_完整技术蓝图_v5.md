# FuzzelGL — 完整技术蓝图 v5（最终落地版）

> 基于 GLFW + OpenGL/Vulkan 双后端的 Windows 10/11 轻量级应用启动器
> 已合并原始策划书 + 第一轮 26 项修复 + 第二轮 26 项修复 + 第三轮自审 30 项修复 = 共 82 项审查闭环。
> 这是可直接落地实现的最终技术方案。

---

## 目录

1. [设计目标与非功能需求](#1-设计目标与非功能需求)
2. [功能规格](#2-功能规格)
3. [用户交互流程](#3-用户交互流程)
4. [分层架构与依赖关系](#4-分层架构与依赖关系)
5. [核心模块接口与实现](#5-核心模块接口与实现)
6. [主循环与启动流程](#6-主循环与启动流程)
7. [构建与部署](#7-构建与部署)
8. [测试与质量保证](#8-测试与质量保证)
9. [里程碑路线图](#9-里程碑路线图)
10. [附录 A：审查闭环清单](#附录-a审查闭环清单)
11. [附录 B：架构决策记录](#附录-b架构决策记录)

---

## 1. 设计目标与非功能需求

### 1.1 体积与链接策略

| 项目 | OpenGL 构建目标 | Vulkan 构建目标 |
|------|----------------|----------------|
| 可执行文件 | ≤ 8 MB | ≤ 10 MB |
| 链接策略 | **动态链接 VC++ 运行时 (`/MD`)** | 同左 |
| 部署 | 附带 `vcruntime140.dll` 等，或要求用户安装 VC++ Redist | 同左 |

### 1.2 性能指标

| 指标 | 目标值 |
|------|--------|
| 窗口呼出响应 | < 50 ms |
| 搜索结果刷新（5000 条） | < 20 ms |
| 搜索结果刷新（低端 CPU 5000 条） | < 30 ms |
| 后台扫描 | 不阻塞 UI 线程 |
| 内存占用（含纹理缓存） | < 45 MB |
| CPU 空闲（窗口隐藏） | < 1% |
| CPU 空闲（窗口可见） | < 5% |

### 1.3 兼容性

- Windows 10 1903+ / Windows 11（x64）
- PerMonitorV2 DPI 感知（应用程序清单声明）
- 高对比度模式基本支持

---

## 2. 功能规格

### 2.1 核心功能

**全局热键呼出/隐藏**

用户可设置自定义组合键（默认 Alt+Space）。在任何应用程序界面按下该组合键，启动器窗口在鼠标所在屏幕中央弹出（通过 `MonitorFromPoint` 判定），伴随缩放淡入动画（200ms，缓动函数 ease-out），输入框自动获得焦点。再次按下相同热键，窗口以缩放淡出动画（150ms，ease-in）隐藏。

**实时模糊搜索（防抖 50ms）**

输入框键入关键词，停止输入 50ms 后触发搜索（debounce）。系统从内存缓存或 SQLite FTS5 中检索，综合排序后实时刷新结果列表。

**拼音首字母搜索**

支持中文拼音首字母匹配（如"微信" → "wx"）。扫描应用时通过独立 `PinyinConverter` 模块生成拼音串存入数据库 `pinyin` 列。支持多音字——存储所有读音的首字母，查询时同时匹配。

**纯键盘操作流程**

| 按键 | 行为 |
|------|------|
| ↑ / ↓ | 上下移动选中项，列表自动滚动 |
| Enter | 启动当前选中项，窗口自动隐藏 |
| Esc | 若输入框非空则清空；若已空则隐藏窗口 |
| 字符键 | 直接输入，触发搜索（50ms 防抖） |

**应用启动抽象层（AppLauncher）**

根据应用类型自动选择启动方式：
- `CreateProcess`：传统 `.exe`
- `ShellExecuteEx`：支持参数、工作目录、请求管理员权限（触发 UAC 提示，非静默提权）
- `IApplicationActivationManager`：UWP 应用（需 `appUserModelId`）

启动后记录启动次数与时间戳，用于后续排序。

**系统托盘常驻**

程序启动后最小化到系统托盘，显示自定义图标。右键菜单包含："显示主窗口"、"设置"、"退出"。

### 2.2 扩展与管理功能

**自定义搜索目录**

用户可在设置页面添加/移除需索引的文件夹（如 `D:\Tools`），程序自动扫描其下的 `.exe`、`.lnk`。

**别名与自定义命令**

用户可为应用设置别名（如 `calc` → 计算器），或创建自定义命令。

自定义命令类型与数据结构：

```cpp
enum class CustomCommandType { Shell, SystemAPI, Script };

struct CustomCommand {
    std::wstring name;          // 显示名称
    std::wstring alias;         // 触发别名（如 "shutdown"）
    CustomCommandType type;

    // Shell 命令
    std::wstring executable;    // 可执行文件路径
    std::wstring arguments;     // 命令行参数
    std::wstring workingDir;    // 工作目录
    bool runAsAdmin = false;    // 是否请求管理员权限

    // SystemAPI 命令
    // type == SystemAPI 时使用，通过枚举选择
    enum SystemAction { Shutdown, Restart, Logoff, Lock, Sleep, Hibernate };
    SystemAction systemAction = Shutdown;

    // Script 命令
    std::wstring scriptPath;    // 脚本文件路径
    // 安全限制：
    // - scriptPath 必须为本地绝对路径（不含网络路径 \\server）
    // - scriptPath 必须在用户指定的白名单目录内
    // - 默认 ScriptCommand 功能禁用，需用户在设置中显式开启
    bool scriptEnabled = false;
};
```

`config.json` 中的存储格式：

```json
{
  "customCommands": [
    {
      "name": "关机",
      "alias": "shutdown",
      "type": "SystemAPI",
      "systemAction": "Shutdown"
    },
    {
      "name": "打开 D 盘工具",
      "alias": "tools",
      "type": "Shell",
      "executable": "explorer.exe",
      "arguments": "D:\\Tools",
      "runAsAdmin": false
    }
  ]
}
```

自定义命令在搜索结果中以特殊图标显示，通过 `AppLauncher::launchCustomCommand()` 执行。

**历史记录与高频优先**

每次启动都会更新 `launch_count` 和 `last_launched`，排序算法综合考虑匹配度与使用频率。

**主题个性化**

设置页面提供：窗口背景色（RGB）及透明度、强调色（选中高亮、光标）、圆角半径、阴影强度。修改后实时预览。

**配置持久化**

用户设置保存为 `config.json`（含热键、目录、别名、主题、自定义命令），启动时自动加载，保存时生成 `.bak` 备份。加载失败时自动回退 `.bak`，再失败则使用默认配置。

**日志与崩溃恢复**

轻量日志系统（Logger）写入 `%APPDATA%/FuzzelGL/logs`；数据库启用 WAL 模式并定期执行 `PRAGMA integrity_check`。独立 `CrashHandler` 模块捕获未处理异常，自包含写入崩溃日志（不依赖 Logger）。

### 2.3 排序算法

```
score = bm25_score + 0.3 * log2(launch_count + 1) + 0.5 * exp(-(now - last_launched) / (7 * 86400))
```

| 项 | 含义 | 范围 |
|----|------|------|
| `bm25_score` | FTS5 rank 函数返回值（已含列权重） | 0 ~ 10 |
| `freq` | `0.3 * log2(launch_count + 1)`，对数增长 | 0 ~ 3 |
| `recency_decay` | 指数衰减，7 天半衰期 | 0 ~ 0.5 |

衰减示例：刚启动 ≈ 0.50；1 天前 ≈ 0.43；7 天前 ≈ 0.18；30 天前 ≈ 0.003；365 天前 ≈ 0.0。

**SQL + C++ 两阶段排序：**

1. SQL 阶段：FTS5 查询按 `bm25_score ASC` 取前 **100** 条（非 20 条），避免高频项被截断。
2. C++ 阶段：对 100 条叠加 `freq + recency_decay`，重排后取前 **20** 条返回 UI。

---

## 3. 用户交互流程

### 3.1 首次启动

1. 程序启动后立即显示托盘图标，主窗口隐藏。
2. 后台线程开始扫描默认目录（开始菜单优先 → 桌面 → 用户自定义），**边扫描边索引**，已扫描部分立即可搜索。
3. 扫描完成前搜索结果可能不完整，但不显示"索引中…"阻塞提示——用户可以立即开始搜索。

### 3.2 呼出与搜索

1. 热键触发 → 主窗口以动画显示，输入框聚焦。
2. 键入字符 → 停止输入 50ms 后触发搜索，从 QueryCache 或数据库获取结果，更新列表。
3. 使用方向键选择，实时高亮。

### 3.3 启动与隐藏

1. 回车 → `AppLauncher` 启动目标，窗口执行隐藏动画。
2. 启动记录异步写入数据库（通过写连接队列）。

### 3.4 配置调整（内嵌 GLFW 页面）

> **v5 统一方案：** 配置面板是主窗口内的一个页面（Page），不是独立 Win32 对话框。完全复用 GLFW 渲染管线，不存在 DPI 不一致或消息泵隔离问题。

1. 托盘右键 → "设置" → 主窗口切换到 `ConfigPage`（由 `UIManager` 管理）。
2. `ConfigPage` 是 `Widget` 的子类，在主窗口内渲染，包含按钮、文本输入框等控件。
3. 修改热键：点击捕获按钮后按下组合键，尝试 `RegisterHotKey`，若失败提示"已被占用"。
4. 增删目录：新目录首次全量扫描，后续变更由 `FileWatcher` 增量监控。
5. 颜色/透明度：实时预览——修改后通过 `ConfigChangeEvent` 通知 `UIManager` 立即应用。
6. 保存/取消：点击"保存"写 `config.json` + `.bak`；点击"取消"恢复原配置。
7. 关闭设置页面后回到搜索主界面。

---

## 4. 分层架构与依赖关系

### 4.1 分层架构（自上而下）

| 层 | 职责 | 模块 |
|----|------|------|
| 应用层 | 主循环、依赖注入、生命周期管理 | `Application` |
| UI 系统层 | 控件树、布局、事件分发、动画、命令生成 | `UIManager`, `Widget` 族, `LayoutEngine`, `AnimationEngine` |
| 资源系统层 | 字体、图标加载与像素数据生成 | `FontManager`, `IconCache`, `ResourceManager` |
| 命令缓冲层 | 绘制与特效命令容器 | `CommandBuffer` (含 `PaintOp`, `EffectOp`) |
| 渲染后端层 | OpenGL/Vulkan 实现，执行命令，管理 GPU 资源 | `IRenderBackend` (派生 `OpenGLBackend`, `VulkanBackend`) |
| 系统服务层 | 窗口、热键、托盘、IME、文件监控、日志、DPI | `WindowManager`, `HotkeyManager`, `TrayManager`, `IMEWrapper`, `FileWatcher`, `Logger`, `CrashHandler` |
| 数据持久层 | SQLite 索引、JSON 配置、拼音、查询缓存 | `IndexDatabase`, `ConfigManager`, `PinyinConverter`, `QueryCache` |
| 工具层 | 跨模块共用工具 | `StringUtils` (`wideToUtf8` / `utf8ToWide`), `LnkResolver`, `AppScanner`, `AppLauncher` |

### 4.2 依赖关系（单向）

- `Application` 依赖所有下层，负责创建并注入依赖。
- UI 层依赖 `CommandBuffer` 和 `ResourceManager`（仅用于获取 `ResourceHandle`）。
- 资源层依赖 `ResourceManager`（注册纹理），不依赖具体渲染 API，仅依赖 `IRenderBackend` 抽象接口。
- 渲染后端依赖 `IRenderBackend` 抽象，内部维护纹理句柄映射，不引用任何上层模块。
- 系统服务层与数据层独立，仅暴露给 `Application`。
- 工具层无依赖方向约束，可被任意层引用。

### 4.3 目录结构

```
FuzzelGL/
├── CMakeLists.txt
├── vcpkg.json
├── cmake/
│   └── FuzzelGL.manifest          # PerMonitorV2 DPI 清单
├── src/
│   ├── app/
│   │   ├── Application.hpp / .cpp
│   │   ├── AppLauncher.hpp / .cpp
│   │   └── CrashHandler.hpp / .cpp
│   ├── ui/
│   │   ├── Widget.hpp             # 基类
│   │   ├── InputField.hpp / .cpp
│   │   ├── ResultList.hpp / .cpp
│   │   ├── ConfigPage.hpp / .cpp
│   │   ├── UIManager.hpp / .cpp
│   │   ├── LayoutEngine.hpp / .cpp
│   │   └── AnimationEngine.hpp / .cpp
│   ├── render/
│   │   ├── ResourceHandle.hpp
│   │   ├── IRenderBackend.hpp
│   │   ├── CommandBuffer.hpp
│   │   ├── OpenGLBackend.hpp / .cpp
│   │   ├── VulkanBackend.hpp / .cpp   # Phase 3
│   │   └── shaders/
│   │       ├── Rect.vert / Rect.frag
│   │       ├── Text.vert / Text.frag
│   │       ├── Blur.vert / Blur.frag      (双 Pass 高斯模糊)
│   │       └── ShadowComposite.frag       (阴影合成)
│   ├── resource/
│   │   ├── ResourceManager.hpp / .cpp
│   │   ├── FontManager.hpp / .cpp
│   │   └── IconCache.hpp / .cpp
│   ├── system/
│   │   ├── WindowManager.hpp / .cpp
│   │   ├── HotkeyManager.hpp / .cpp
│   │   ├── TrayManager.hpp / .cpp
│   │   ├── IMEWrapper.hpp / .cpp
│   │   ├── FileWatcher.hpp / .cpp
│   │   └── Logger.hpp / .cpp
│   ├── data/
│   │   ├── IndexDatabase.hpp / .cpp
│   │   ├── ConfigManager.hpp / .cpp
│   │   ├── PinyinConverter.hpp / .cpp
│   │   ├── QueryCache.hpp / .cpp
│   │   ├── AppScanner.hpp / .cpp
│   │   └── LnkResolver.hpp / .cpp
│   ├── utils/
│   │   └── StringUtils.hpp / .cpp
│   └── main.cpp
├── third_party/
│   └── mini-pinyin/               # vendor 的轻量拼音库（非 vcpkg）
│       ├── pinyin.h
│       └── pinyin_data.h          # 字典数据
├── tests/
│   ├── test_fontmanager.cpp
│   ├── test_pinyin.cpp
│   ├── test_querycache.cpp
│   ├── test_resourcemanager.cpp
│   ├── test_stringutils.cpp
│   └── test_scanner.cpp
└── assets/
    └── default_icon.png           # 占位图标
```

---

## 5. 核心模块接口与实现

### 5.1 ResourceHandle 类型定义

```cpp
// src/render/ResourceHandle.hpp
#pragma once
#include <cstdint>

namespace fuzzel {

/// Opaque handle for a texture registered with ResourceManager.
/// 0 = invalid/null handle.
using ResourceHandle = uint64_t;

} // namespace fuzzel
```

### 5.2 ResourceManager 资源管理器

> **v5 关键设计：** 持有像素数据深拷贝（`std::vector<uint8_t>`），避免 resync 时悬空指针。

```cpp
// src/resource/ResourceManager.hpp
#pragma once
#include "render/ResourceHandle.hpp"
#include "render/IRenderBackend.hpp"  // IRenderBackend* 前向声明即可
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <set>
#include <mutex>

namespace fuzzel {

struct TextureDesc {
    int width = 0;
    int height = 0;
    enum Format { R8, RGBA8 } format = RGBA8;
    std::vector<uint8_t> pixels;  // 深拷贝，ResourceManager 拥有所有权
};

class IRenderBackend;  // 前向声明

class ResourceManager {
public:
    ResourceManager();
    ~ResourceManager();

    void setBackend(IRenderBackend* backend);

    /// 注册持久纹理（字体图集等）。返回非零句柄。
    ResourceHandle registerTexture(const TextureDesc& desc);

    /// 注销持久纹理。
    void unregisterTexture(ResourceHandle handle);

    /// 分配临时纹理（用于阴影/特效离屏渲染），帧结束后自动回收。
    ResourceHandle allocateTemporary(int w, int h, TextureDesc::Format fmt = TextureDesc::RGBA8);

    /// 每帧结束后调用，回收本帧分配的所有临时纹理。
    void recycleTemporaries();

    /// 后端重建时重新同步所有持久纹理到新后端。
    void resyncAllTextures();

private:
    uint64_t m_nextHandle = 1;
    IRenderBackend* m_backend = nullptr;

    std::unordered_map<ResourceHandle, TextureDesc> m_registry;     // 持久纹理
    std::set<ResourceHandle> m_temporaryTextures;                    // 本帧临时纹理
    std::mutex m_mutex;                                              // 注册/注销可能从主线程调用
};

} // namespace fuzzel
```

**生命周期规则：**
- `registerTexture`：存入 `m_registry`，若 `m_backend` 非空则回调 `onTextureRegistered`。
- `unregisterTexture`：从 `m_registry` 移除，回调 `onTextureUnregistered`。
- `allocateTemporary`：注册到 `m_registry` + `m_temporaryTextures`，回调 `onTextureRegistered`。
- `recycleTemporaries`：遍历 `m_temporaryTextures`，调用 `unregisterTexture`（回调 `onTextureUnregistered`），清空集合。
- `resyncAllTextures`：遍历 `m_registry`，对每个条目回调 `onTextureRegistered`，供后端重建 GPU 资源。因为 `pixels` 是深拷贝，不会悬空。

---

### 5.3 IRenderBackend 渲染后端抽象

```cpp
// src/render/IRenderBackend.hpp
#pragma once
#include "render/ResourceHandle.hpp"
#include "render/CommandBuffer.hpp"
#include <cstdint>

struct GLFWwindow;

namespace fuzzel {

struct TextureDesc;  // 前向声明

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    virtual bool init(GLFWwindow* window) = 0;
    virtual void resize(int w, int h) = 0;
    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;
    virtual void present() = 0;

    // 资源回调（由 ResourceManager 调用）
    virtual void onTextureRegistered(ResourceHandle handle, const TextureDesc& desc) = 0;
    virtual void onTextureUnregistered(ResourceHandle handle) = 0;

    // 标记某纹理在本帧被使用（用于 Vulkan Fence 追踪）
    virtual void notifyResourceUsed(ResourceHandle handle) = 0;

    // 执行命令缓冲
    virtual void execute(const CommandBuffer& cmds) = 0;

    // 帧结束时回收延迟删除的资源（仅 Vulkan 需要实质实现）
    virtual void collectGarbage(uint64_t frameIndex) = 0;
};

} // namespace fuzzel
```

**后端差异——延迟释放策略：**

| 后端 | 延迟释放机制 | 理由 |
|------|-------------|------|
| **OpenGL** | 直接 `glDeleteTextures`，无延迟 | 驱动内部引用计数管理，GPU 仍在使用时不会立即释放 |
| **Vulkan** | Fence 追踪 + 帧计数延迟 | `vkDestroyImage` 不会检查 GPU 是否仍在使用，必须等 Fence 信号 |

**Vulkan Fence 追踪逻辑：**

```cpp
// VulkanBackend 内部维护
struct TextureEntry {
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
    uint64_t lastUsedFrame;  // 最后被使用的帧号（由 notifyResourceUsed 更新）
};

// onTextureUnregistered:
void VulkanBackend::onTextureUnregistered(ResourceHandle handle) {
    auto it = m_textureMap.find(handle);
    if (it != m_textureMap.end()) {
        m_retiredTextures.push_back({it->second, m_frameIndex});  // 退役，记录退役帧
        m_textureMap.erase(it);
    }
}

// collectGarbage:
void VulkanBackend::collectGarbage(uint64_t frameIndex) {
    // 等待对应帧的 Fence 信号后才能释放
    // 三缓冲下，frameIndex - retiredFrame >= 3 表示 GPU 已完成
    for (auto it = m_retiredTextures.begin(); it != m_retiredTextures.end(); ) {
        if (frameIndex - it->retiredFrame >= 3) {
            // 确认对应 Command Buffer 的 Fence 已 signaled
            VkFence fence = m_frameFences[it->retiredFrame % MAX_FRAMES_IN_FLIGHT];
            if (vkGetFenceStatus(m_device, fence) == VK_SUCCESS) {
                vkDestroyImage(m_device, it->entry.image, nullptr);
                vkDestroyImageView(m_device, it->entry.view, nullptr);
                vkFreeMemory(m_device, it->entry.memory, nullptr);
                it = m_retiredTextures.erase(it);
            } else {
                ++it;
            }
        } else {
            ++it;
        }
    }
}
```

**execute() 批次合并策略：**

```
1. 线性遍历 PaintOp 列表（不排序，保持语义正确性）。
2. 遇到连续的"可合并"操作（RectOp, RoundedRectOp, TextRunOp, IconOp）且
   (blendMode, clipOp, textureHandle) 相同时，收集顶点数据到本地缓冲区。
3. 遇到以下情况提交当前批次并开始新批次：
   - blendMode 变化
   - clipOp 变化（包括 monostate → Rect 或 Rect → monostate）
   - textureHandle 变化
   - 遇到 RenderTargetOp（切换 FBO/Render Pass）
   - 遇到 ApplyEffectOp（特效 Pass）
   - 遇到 ShadowOp（阴影 Pass）
4. RenderTargetOp 和 ApplyEffectOp 不参与排序合并，它们是"状态切换点"，
   总是拆分批次。
5. ShadowOp 输出到当前激活的 RenderTarget（FBO），不创建额外输出纹理。
```

**ClipOp 实现方式：**

| ClipOp 类型 | OpenGL 实现 | Vulkan 实现 | 批次影响 |
|-------------|------------|------------|---------|
| `monostate`（无裁剪） | 禁用 `glScissor` | 禁用 `scissorTest` | 总是拆分批次 |
| `Rect` | `glScissor(x, y, w, h)` | `VkRect2D` | 总是拆分批次 |
| `RoundedRect` | Stencil Buffer：先画圆角 mask 再裁剪 | Stencil Buffer 同理 | 总是拆分批次 |

`monostate` 表示"无裁剪"，不继承前一个命令的裁剪状态。

---

### 5.4 CommandBuffer 命令缓冲

```cpp
// src/render/CommandBuffer.hpp
#pragma once
#include "render/ResourceHandle.hpp"
#include <glm/glm.hpp>
#include <variant>
#include <vector>
#include <array>
#include <cstdint>

namespace fuzzel {

enum class BlendMode { Normal, Multiply, Screen };

// ── ClipOp ──────────────────────────────────
struct ClipRect { int x, y, w, h; };
struct ClipRoundedRect { int x, y, w, h; float radius; };
using ClipOp = std::variant<std::monostate, ClipRect, ClipRoundedRect>;

// ── 单个字形顶点（标准 GPU 顶点格式：1 pos + 1 uv） ──
struct GlyphVertex {
    glm::vec2 pos;   // 屏幕坐标
    glm::vec2 uv;    // 字形图集中的 UV
};
// 每个字形 = 4 个 GlyphVertex + 6 个索引（两个三角形）

// ── PaintOp 变体 ──────────────────────────────
struct RectOp {
    glm::vec2 pos, size;
    glm::vec4 color;
};

struct RoundedRectOp {
    glm::vec2 pos, size;
    float radius;
    glm::vec4 color;
};

struct TextRunOp {
    ResourceHandle textureHandle;       // 字形图集纹理
    std::vector<GlyphVertex> vertices;  // 每个字形 4 个顶点
    std::vector<uint32_t> indices;      // 每个字形 6 个索引
    glm::vec4 color;
    float opacity;
};

struct IconOp {
    ResourceHandle textureHandle;
    glm::vec2 pos, size;
    glm::vec4 tintColor;
};

struct ShadowOp {
    ResourceHandle sourceTexture;  // 由 ResourceManager::allocateTemporary() 获取
    float blurRadius;
    glm::vec2 offset;
    glm::vec4 color;
    // 输出：混合到当前激活的 RenderTarget（FBO），不创建额外输出纹理。
};

struct RenderTargetOp {
    enum Action { Set, Reset, Blit };
    // Set:   将渲染目标切换到 targetTexture（离屏纹理）
    // Reset: 将渲染目标切换回窗口
    // Blit:  将 srcTexture 的内容复制到 targetTexture
    Action action;
    ResourceHandle targetTexture;  // Set: 目标纹理；Reset: 忽略；Blit: 目标纹理
    ResourceHandle srcTexture;     // 仅 Blit 使用，源纹理
    glm::vec4 clearColor;          // 仅 Set 使用
};

struct ApplyEffectOp {
    enum EffectType { GaussianBlur, Glow, ColorShift };
    EffectType type;
    float intensity;
    ResourceHandle inputTexture;   // 由 allocateTemporary() 获取
    ResourceHandle outputTexture;  // 由 allocateTemporary() 获取，不能为 0
};

using PaintOp = std::variant<
    RectOp, RoundedRectOp, TextRunOp, IconOp,
    ShadowOp, RenderTargetOp, ApplyEffectOp
>;

// ── RenderCommand ────────────────────────────
struct RenderCommand {
    PaintOp op;
    BlendMode blend = BlendMode::Normal;
    ClipOp clip;   // monostate = 无裁剪（不继承前一条命令的裁剪）
};

// ── CommandBuffer ────────────────────────────
class CommandBuffer {
public:
    void clear();
    void add(const RenderCommand& cmd);
    const std::vector<RenderCommand>& getCommands() const { return m_commands; }

private:
    std::vector<RenderCommand> m_commands;
};

} // namespace fuzzel
```

**离屏纹理使用流程：**

```
1. 需要阴影/特效时：
   auto tex = resMgr->allocateTemporary(w, h);
2. 切换渲染目标到 tex：
   cmdBuf.add({RenderTargetOp{Set, tex, 0, clearColor}});
3. 绘制内容到 tex（正常的 RectOp/TextOp/IconOp...）
4. 恢复渲染目标到窗口：
   cmdBuf.add({RenderTargetOp{Reset, 0, 0, {}}});
5. 应用阴影/特效：
   cmdBuf.add({ShadowOp{sourceTexture: tex, blurRadius, offset, color}});
   或
   cmdBuf.add({ApplyEffectOp{GaussianBlur, intensity, inputTexture: tex, outputTexture: tex2}});
6. 帧结束后：
   resMgr->recycleTemporaries();  // 自动回收 tex 和 tex2
```

---

### 5.5 FontManager 字体排版

```cpp
// src/resource/FontManager.hpp
#pragma once
#include "render/ResourceHandle.hpp"
#include "resource/ResourceManager.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace fuzzel {

struct GlyphInfo {
    ResourceHandle atlasHandle;  // 所属图集纹理句柄
    glm::vec2 uvMin, uvMax;     // 在图集中的 UV 范围
    glm::vec2 size;              // 字形像素尺寸
    glm::vec2 bearing;           // 基线偏移
    float advance;               // 字符宽度
};

class FontManager {
public:
    FontManager(ResourceManager* resMgr);
    ~FontManager();

    bool loadFont(const std::wstring& path, float pixelSize);

    /// 创建文本排版结果。返回字形顶点数据，可直接用于 TextRunOp。
    struct TextRun {
        ResourceHandle atlasHandle;
        std::vector<GlyphVertex> vertices;
        std::vector<uint32_t> indices;
        float totalWidth;
        float totalHeight;
    };
    TextRun createTextRun(const std::wstring& text, float fontSize,
                          float maxWidth, const glm::vec4& color);

private:
    ResourceManager* m_resMgr;

    // 字体图集管理（多图集，不驱逐）
    struct Atlas {
        ResourceHandle handle;
        int usedWidth = 0;   // 当前行已用宽度
        int usedHeight = 0;  // 当前行高度
        int rowHeight = 0;   // 当前行最大字形高度
    };
    std::vector<Atlas> m_atlases;  // 8 张图集上限
    static constexpr int ATLAS_SIZE = 2048;
    static constexpr int MAX_ATLASES = 8;

    // 字形缓存
    // key = (codepoint << 16) | fontSizeBucket
    // fontSizeBucket = (int)(fontSize / 2) * 2，2px 粒度分桶
    // 例：fontSize 32 → bucket 32；fontSize 33 → bucket 32；fontSize 35 → bucket 34
    std::unordered_map<uint64_t, GlyphInfo> m_glyphCache;

    FT_Library m_ft = nullptr;
    FT_Face m_face = nullptr;

    const GlyphInfo& getGlyph(uint32_t codepoint, float fontSize);
    Atlas& getOrCreateAtlas(int glyphW, int glyphH);
};

} // namespace fuzzel
```

**图集满时策略：**

当一张图集的当前行放不下新字形时：
1. 换行（`usedWidth = 0`, `usedHeight += rowHeight`）。
2. 若图集高度也用完，创建新图集（最多 8 张）。
3. 8 张图集全部用完时，**不驱逐**——而是增大图集尺寸（2048 → 4096），重建所有图集。这种情况极罕见（需要渲染极大量不同字号/CJK 字符），重建在帧间进行，可接受短暂卡顿。

`fontSizeBucket` 定义：`bucket = (int)(fontSize / 2) * 2`，2px 粒度。32px 和 33px 共享同一套字形，视觉差异可忽略。

---

### 5.6 IconCache 图标缓存

> **v5 关键修复：** 异步线程**不调用 GL 函数**。只提取像素数据，回传主线程队列。

```cpp
// src/resource/IconCache.hpp
#pragma once
#include "render/ResourceHandle.hpp"
#include "resource/ResourceManager.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <deque>

namespace fuzzel {

class UIManager;  // 前向声明

class IconCache {
public:
    IconCache(ResourceManager* resMgr, size_t capacity = 256);
    ~IconCache();

    /// 请求图标。若缓存命中则同步返回，否则返回 placeholder 并异步加载。
    /// 加载完成后回调在主线程被调用（通过 processPending()）。
    ResourceHandle requestIcon(const std::wstring& path,
                               std::function<void(ResourceHandle)> onLoaded = nullptr);

    /// 主线程每帧调用：消费已完成的异步加载结果，注册纹理，触发回调。
    void processPending();

    /// 通知 UI 需要重绘（图标加载完成后调用）。
    void setInvalidateCallback(std::function<void()> cb) { m_invalidateCb = std::move(cb); }

private:
    ResourceManager* m_resMgr;
    size_t m_capacity;

    // LRU 缓存
    struct CacheEntry {
        ResourceHandle handle;
        size_t lastUsed;
    };
    std::unordered_map<std::wstring, CacheEntry> m_cache;
    size_t m_clock = 0;

    // 异步加载线程
    std::thread m_worker;
    std::atomic<bool> m_running{true};
    std::mutex m_queueMutex;

    // 请求队列（主线程 → 工作线程）
    struct LoadRequest {
        std::wstring path;
    };
    std::deque<LoadRequest> m_requestQueue;

    // 结果队列（工作线程 → 主线程）
    struct LoadResult {
        std::wstring path;
        std::vector<uint8_t> pixels;  // RGBA 像素数据
        int width;
        int height;
        std::vector<std::function<void(ResourceHandle)>> callbacks;  // 所有等待此图标的回调
    };
    std::deque<LoadResult> m_resultQueue;

    // 去重：path → 回调列表
    std::unordered_map<std::wstring, std::vector<std::function<void(ResourceHandle)>>> m_pending;

    ResourceHandle m_placeholder;  // 占位纹理
    std::function<void()> m_invalidateCb;

    void workerLoop();
    bool extractIconPixels(const std::wstring& path,
                           std::vector<uint8_t>& outPixels,
                           int& outW, int& outH);
    void evictIfNeeded();
};

} // namespace fuzzel
```

**异步加载流程：**

```
主线程:
  requestIcon("C:\\app.exe", cb)
    → 缓存命中？返回 handle
    → m_pending 中已有此 path？追加 cb 到 callback_list
    → 否则：加入 m_requestQueue，创建 m_pending[path] = {cb}
    → 返回 m_placeholder

工作线程:
  workerLoop()
    → 取出 LoadRequest
    → extractIconPixels(path)  // 调用 SHGetFileInfo，提取 HICON → RGBA 像素
    → 将 LoadResult{path, pixels, w, h, callbacks} 推入 m_resultQueue

主线程（每帧）:
  processPending()
    → 取出 LoadResult
    → registerTexture(pixels)  // 在主线程调用 GL 函数，安全
    → 存入 m_cache
    → 逐一调用 callbacks
    → 调用 m_invalidateCb()  // 通知 UIManager markDirty()
    → 从 m_pending 移除
```

---

### 5.7 UIManager 与控件体系

**Widget 基类：**

```cpp
// src/ui/Widget.hpp
#pragma once
#include "render/CommandBuffer.hpp"
#include <glm/glm.hpp>
#include <string>
#include <functional>
#include <optional>

namespace fuzzel {

struct MouseEvent {
    enum Type { Press, Release, Move };
    Type type;
    glm::vec2 pos;
    int button;  // 0=left, 1=right, 2=middle
};

struct KeyEvent {
    int key;       // GLFW key code
    int scancode;
    int action;    // GLFW_PRESS, GLFW_RELEASE, GLFW_REPEAT
    int mods;      // GLFW mods
};

struct CharEvent {
    uint32_t codepoint;
};

class Widget {
public:
    virtual ~Widget() = default;

    virtual void layout(const glm::vec2& pos, const glm::vec2& size) {
        m_pos = pos;
        m_size = size;
    }

    virtual void paint(CommandBuffer& cmdBuf) = 0;

    // 事件处理（默认返回 false = 未处理）
    virtual bool onMouseEvent(const MouseEvent& evt) { return false; }
    virtual bool onKeyEvent(const KeyEvent& evt) { return false; }
    virtual bool onCharEvent(const CharEvent& evt) { return false; }

    // 命中测试：返回此坐标是否在本 Widget 范围内
    virtual bool hitTest(const glm::vec2& pos) const {
        return pos.x >= m_pos.x && pos.x <= m_pos.x + m_size.x &&
               pos.y >= m_pos.y && pos.y <= m_pos.y + m_size.y;
    }

    const glm::vec2& getPos() const { return m_pos; }
    const glm::vec2& getSize() const { return m_size; }
    void setVisible(bool v) { m_visible = v; }
    bool isVisible() const { return m_visible; }

protected:
    glm::vec2 m_pos{0, 0};
    glm::vec2 m_size{0, 0};
    bool m_visible = true;
};

} // namespace fuzzel
```

**UIManager：**

```cpp
// src/ui/UIManager.hpp
#pragma once
#include "ui/Widget.hpp"
#include "ui/InputField.hpp"
#include "ui/ResultList.hpp"
#include "ui/ConfigPage.hpp"
#include "ui/AnimationEngine.hpp"
#include "ui/LayoutEngine.hpp"
#include "resource/FontManager.hpp"
#include "resource/IconCache.hpp"
#include "resource/ResourceManager.hpp"
#include <memory>
#include <vector>
#include <functional>

namespace fuzzel {

class UIManager {
public:
    UIManager(FontManager* fontMgr, IconCache* iconCache,
              ResourceManager* resMgr, float dpiScale = 1.0f);
    ~UIManager();

    // 页面切换
    void showSearchPage();
    void showConfigPage();
    ConfigPage* getConfigPage() { return m_configPage.get(); }

    // 更新动画
    void update(float delta);

    // 生成绘制命令
    void paint(CommandBuffer& cmdBuf);

    // 事件分发
    bool onMouseEvent(const MouseEvent& evt);
    bool onKeyEvent(const KeyEvent& evt);
    bool onCharEvent(const CharEvent& evt);

    // 输入框内容
    void setSearchQuery(const std::wstring& query);
    std::wstring getSearchQuery() const;

    // 结果列表
    void setSearchResults(const std::vector<SearchResultEntry>& results);
    SearchResultEntry getSelectedResult() const;

    // 标记需要重绘
    void markDirty() { m_dirty = true; }
    bool isDirty() const { return m_dirty; }
    void clearDirty() { m_dirty = false; }

    // DPI
    void setDpiScale(float scale);
    float getDpiScale() const { return m_dpiScale; }

    // 动画
    AnimationEngine& getAnimation() { return m_anim; }

    // 搜索回调（由 Application 设置）
    using SearchCallback = std::function<void(const std::wstring&)>;
    void setSearchCallback(SearchCallback cb) { m_searchCb = std::move(cb); }

    // 启动回调
    using LaunchCallback = std::function<void(const SearchResultEntry&)>;
    void setLaunchCallback(LaunchCallback cb) { m_launchCb = std::move(cb); }

private:
    FontManager* m_fontMgr;
    IconCache* m_iconCache;
    ResourceManager* m_resMgr;
    float m_dpiScale;

    // 页面 Widget
    std::unique_ptr<Widget> m_searchRoot;   // 搜索页根 Widget
    std::unique_ptr<ConfigPage> m_configPage; // 配置页

    // 子控件引用（由 m_searchRoot 持有）
    InputField* m_inputField = nullptr;
    ResultList* m_resultList = nullptr;

    // 状态
    enum class Page { Search, Config } m_currentPage = Page::Search;
    bool m_dirty = true;

    AnimationEngine m_anim;
    LayoutEngine m_layout;

    SearchCallback m_searchCb;
    LaunchCallback m_launchCb;

    Widget* getActiveRoot();
    void layoutPage();
};

} // namespace fuzzel
```

**鼠标事件路由：** UIManager 收到 `MouseEvent` 后，对 `getActiveRoot()` 的子 Widget 树做 hit-test 遍历（从顶层到底层），第一个 `hitTest() == true` 的 Widget 接收事件。

**键盘事件路由：** 键盘事件发送到当前焦点 Widget（`InputField` 或 `ConfigPage` 中的控件）。若焦点 Widget 未处理，回退到 `UIManager` 处理全局快捷键（如 Esc）。

---

### 5.8 LayoutEngine 布局引擎

```cpp
// src/ui/LayoutEngine.hpp
#pragma once
#include "ui/Widget.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace fuzzel {

class LayoutEngine {
public:
    // 垂直排列子 Widget
    void layoutVertical(Widget* parent,
                        const std::vector<Widget*>& children,
                        float padding, float spacing);

    // 水平排列
    void layoutHorizontal(Widget* parent,
                          const std::vector<Widget*>& children,
                          float padding, float spacing);

    // 居中
    void centerInParent(Widget* child, const glm::vec2& parentSize);

    // 设置窗口尺寸并触发布局
    void resize(const glm::vec2& windowSize, float dpiScale);

    const glm::vec2& getWindowSize() const { return m_windowSize; }

private:
    glm::vec2 m_windowSize{800, 600};
};

} // namespace fuzzel
```

第一版使用固定坐标布局（输入框固定在顶部，结果列表填充剩余空间），后续可扩展流式布局。

---

### 5.9 AnimationEngine 动画引擎

```cpp
// src/ui/AnimationEngine.hpp
#pragma once
#include <glm/glm.hpp>
#include <functional>
#include <vector>
#include <string>
#include <cstdint>

namespace fuzzel {

enum class Easing { Linear, EaseOut, EaseIn, EaseInOut };

class AnimationEngine {
public:
    using TargetSetter = std::function<void(float)>;

    // 添加一个属性动画
    // duration 单位秒。setter 每帧被调用，参数为插值后的值。
    uint64_t addTween(float from, float to, float duration,
                      TargetSetter setter, Easing easing = Easing::EaseOut);

    // 添加延迟回调
    uint64_t addDelay(float delay, std::function<void()> callback);

    // 取消动画
    void cancel(uint64_t id);

    // 每帧更新
    void update(float delta);

    bool hasActive() const { return !m_tweens.empty(); }

private:
    struct Tween {
        uint64_t id;
        float from;
        float to;
        float duration;
        float elapsed = 0;
        TargetSetter setter;
        Easing easing;
    };
    std::vector<Tween> m_tweens;

    struct DelayedCallback {
        uint64_t id;
        float remaining;
        std::function<void()> callback;
    };
    std::vector<DelayedCallback> m_delayed;

    uint64_t m_nextId = 1;

    float applyEasing(Easing e, float t);
};

} // namespace fuzzel
```

**使用示例（窗口显示动画）：**

```cpp
// 缩放淡入：200ms ease-out
m_anim.addTween(0.0f, 1.0f, 0.2f,
    [this](float v) {
        m_windowScale = v;
        m_windowOpacity = v;
        m_uiMgr->markDirty();
    }, Easing::EaseOut);
```

---

### 5.10 输入事件分发

```cpp
// src/system/Events.hpp
#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>

namespace fuzzel {

// 事件类型与 §5.7 Widget.hpp 中的 MouseEvent/KeyEvent/CharEvent 一致
// 此文件定义 WindowManager → Application → UIManager 的传递接口

struct InputState {
    bool imeActive = false;  // IME 预编辑状态中
    std::wstring imePreEdit; // 预编辑文本
};

} // namespace fuzzel
```

**事件流：**

```
GLFW 回调 → WindowManager（静态 trampoline → 实例方法）
  → Application::onKey / onChar / onMouse
    → IME 激活时：
        - onChar: 忽略（IME 接管字符输入）
        - onKey: 仅处理 Esc（清空预编辑或隐藏窗口）
    → IME 未激活时：
        - onChar: 转发到 UIManager::onCharEvent
        - onKey: 转发到 UIManager::onKeyEvent
    → onMouse: 直接转发到 UIManager::onMouseEvent
```

---

### 5.11 WindowManager 窗口管理

```cpp
// src/system/WindowManager.hpp
#pragma once
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <string>
#include <functional>

namespace fuzzel {

class WindowManager {
public:
    WindowManager(int width, int height, const std::string& title);
    ~WindowManager();

    bool init();
    void shutdown();

    GLFWwindow* getGLFWWindow() { return m_window; }
    HWND getHWND() const { return m_hwnd; }

    bool shouldClose() const;
    void swapBuffers();

    // 显示/隐藏
    void show();
    void hide();
    void toggleVisibility();
    bool isVisible() const { return m_visible; }

    // DPI
    float getDpiScale() const { return m_dpiScale; }
    void updateDpiScale();

    // 窗口位置（鼠标所在显示器中央）
    void centerOnMouseMonitor();

    // 回调注册
    using KeyCallback = std::function<void(int, int, int, int)>;
    using CharCallback = std::function<void(uint32_t)>;
    using MouseCallback = std::function<void(int, int, int)>;
    using CursorCallback = std::function<void(double, double)>;

    void setKeyCallback(KeyCallback cb) { m_keyCb = std::move(cb); }
    void setCharCallback(CharCallback cb) { m_charCb = std::move(cb); }
    void setMouseCallback(MouseCallback cb) { m_mouseCb = std::move(cb); }
    void setCursorCallback(CursorCallback cb) { m_cursorCb = std::move(cb); }

    // IME 状态
    void setImeActive(bool active) { m_imeActive = active; }
    bool isImeActive() const { return m_imeActive; }

private:
    GLFWwindow* m_window = nullptr;
    HWND m_hwnd = nullptr;
    bool m_visible = false;
    float m_dpiScale = 1.0f;
    bool m_imeActive = false;

    KeyCallback m_keyCb;
    CharCallback m_charCb;
    MouseCallback m_mouseCb;
    CursorCallback m_cursorCb;

    // 静态 trampoline
    static void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void charCallback(GLFWwindow* w, uint32_t codepoint);
    static void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* w, double x, double y);

    // GLFW 回调注册（在 init() 中调用）
    void registerCallbacks();

    // 窗口过程子类化（用于 WM_DPICHANGED 和 IME）
    static WNDPROC s_originalProc;
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};

} // namespace fuzzel
```

**GLFW 回调注册（`init()` 中调用）：**

```cpp
void WindowManager::registerCallbacks() {
    glfwSetKeyCallback(m_window, &WindowManager::keyCallback);
    glfwSetCharCallback(m_window, &WindowManager::charCallback);
    glfwSetMouseButtonCallback(m_window, &WindowManager::mouseButtonCallback);
    glfwSetCursorPosCallback(m_window, &WindowManager::cursorPosCallback);
}
```

**窗口过程子类化（处理 `WM_DPICHANGED` 和 IME）：**

```cpp
WNDPROC WindowManager::s_originalProc = nullptr;

void WindowManager::subclassWindow() {
    m_hwnd = glfwGetWin32Window(m_window);
    s_originalProc = (WNDPROC)SetWindowLongPtr(m_hwnd, GWLP_WNDPROC,
                                                (LONG_PTR)&WindowManager::windowProc);
}

LRESULT CALLBACK WindowManager::windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_DPICHANGED: {
            int newDpi = HIWORD(wp);
            // 更新 DPI 缩放
            // 通知 Application/UIManager 重新布局
            if (s_dpiChangedCallback) s_dpiChangedCallback(newDpi);
            // 应用建议的窗口矩形
            RECT* rect = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, rect->left, rect->top,
                         rect->right - rect->left, rect->bottom - rect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_IME_STARTCOMPOSITION:
        case WM_IME_COMPOSITION:
        case WM_IME_ENDCOMPOSITION:
            // 由 IMEWrapper 处理，这里只阻止 GLFW 默认处理
            if (s_imeCallback) s_imeCallback(msg, wp, lp);
            return 0;
    }
    return CallWindowProc(s_originalProc, hwnd, msg, wp, lp);
}
```

---

### 5.12 HotkeyManager 热键管理

```cpp
// src/system/HotkeyManager.hpp
#pragma once
#include <windows.h>
#include <functional>
#include <string>

namespace fuzzel {

struct HotkeyConfig {
    UINT modifiers = MOD_ALT;
    UINT vk = VK_SPACE;  // Alt+Space
};

class HotkeyManager {
public:
    HotkeyManager();
    ~HotkeyManager();

    bool register(const HotkeyConfig& config);
    void unregister();

    // 检查消息是否为热键消息（在主循环中调用）
    // 返回 true 表示热键被触发
    bool checkMessage(MSG& msg);

    // 热键回调
    using HotkeyCallback = std::function<void()>;
    void setCallback(HotkeyCallback cb) { m_cb = std::move(cb); }

    // 尝试注册热键（用于设置面板测试）
    static bool tryRegister(const HotkeyConfig& config);

private:
    static constexpr int HOTKEY_ID = 0xF001;
    HotkeyConfig m_config;
    bool m_registered = false;
    HotkeyCallback m_cb;
};

} // namespace fuzzel
```

**消息优先级：** 主循环先检查 `WM_HOTKEY`。IME 激活时，热键仍可触发（如 Alt+Space 呼出窗口），但 Esc 由 IME 优先处理（清空预编辑文本）。

---

### 5.13 TrayManager 系统托盘

> **v5 明确：** TrayManager 创建独立的隐藏消息窗口接收托盘消息，不与 GLFW 窗口冲突。

```cpp
// src/system/TrayManager.hpp
#pragma once
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <functional>

namespace fuzzel {

class TrayManager {
public:
    TrayManager();
    ~TrayManager();

    bool init(HICON icon, const std::wstring& tooltip);
    void remove();

    // 托盘回调
    using TrayCallback = std::function<void()>;
    void setShowCallback(TrayCallback cb) { m_showCb = std::move(cb); }
    void setConfigCallback(TrayCallback cb) { m_configCb = std::move(cb); }
    void setQuitCallback(TrayCallback cb) { m_quitCb = std::move(cb); }

    // 处理托盘消息（在主循环中调用）
    // 返回 true 表示消息被处理
    bool processMessage(MSG& msg);

private:
    HWND m_hwnd = nullptr;           // 独立隐藏消息窗口
    NOTIFYICONDATAW m_nid{};
    UINT m_taskbarCreatedMsg = 0;    // TaskbarCreated 消息 ID

    static const UINT WM_TRAYICON = WM_USER + 1;

    TrayCallback m_showCb;
    TrayCallback m_configCb;
    TrayCallback m_quitCb;

    bool createHiddenWindow();
    static LRESULT CALLBACK trayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};

} // namespace fuzzel
```

**独立隐藏窗口创建：**

```cpp
bool TrayManager::createHiddenWindow() {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = &TrayManager::trayWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"FuzzelGLTray";
    RegisterClassW(&wc);

    m_hwnd = CreateWindowExW(0, L"FuzzelGLTray", L"", 0,
                             0, 0, 0, 0, HWND_MESSAGE, nullptr,
                             GetModuleHandle(nullptr), nullptr);
    return m_hwnd != nullptr;
}

LRESULT CALLBACK TrayManager::trayWndProc(HWND hwnd, UINT msg,
                                          WPARAM wp, LPARAM lp) {
    if (msg == WM_TRAYICON) {
        if (LOWORD(lp) == WM_RBUTTONUP) {
            // 显示右键菜单
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"显示主窗口");
            AppendMenuW(menu, MF_STRING, 2, L"设置");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 3, L"退出");
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                     pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            // cmd 回调分发由 processMessage 处理
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
```

---

### 5.14 IMEWrapper 中文输入法

```cpp
// src/system/IMEWrapper.hpp
#pragma once
#include <windows.h>
#include <imm.h>
#include <string>
#include <functional>
#include <glm/glm.hpp>

namespace fuzzel {

class IMEWrapper {
public:
    IMEWrapper();
    ~IMEWrapper();

    void init(HWND hwnd);

    // 处理 IME 窗口消息（由 WindowManager 的 windowProc 调用）
    // 返回 true 表示已处理
    bool handleMessage(UINT msg, WPARAM wp, LPARAM lp);

    // 设置候选窗口位置（屏幕坐标）
    void setCompositionWindowPos(const glm::vec2& screenPos);

    // 事件回调
    using PreEditCallback = std::function<void(const std::wstring&)>;
    using CommitCallback = std::function<void(const std::wstring&)>;
    using EndCallback = std::function<void()>;

    void setPreEditCallback(PreEditCallback cb) { m_preEditCb = std::move(cb); }
    void setCommitCallback(CommitCallback cb) { m_commitCb = std::move(cb); }
    void setEndCallback(EndCallback cb) { m_endCb = std::move(cb); }

    bool isActive() const { return m_active; }

private:
    HWND m_hwnd = nullptr;
    HIMC m_himc = nullptr;
    bool m_active = false;

    PreEditCallback m_preEditCb;
    CommitCallback m_commitCb;
    EndCallback m_endCb;

    std::wstring extractComposingText(HIMC himc);
    std::wstring extractResultText(HIMC himc);
};

} // namespace fuzzel
```

**核心 IMM 实现：**

```cpp
// src/system/IMEWrapper.cpp
#include "system/IMEWrapper.hpp"
#include <vector>

namespace fuzzel {

void IMEWrapper::init(HWND hwnd) {
    m_hwnd = hwnd;
    m_himc = ImmGetContext(hwnd);
    if (m_himc) {
        ImmReleaseContext(hwnd, m_himc);
    }
}

bool IMEWrapper::handleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_IME_STARTCOMPOSITION: {
            m_active = true;
            m_himc = ImmGetContext(m_hwnd);
            return true;
        }
        case WM_IME_COMPOSITION: {
            m_himc = ImmGetContext(m_hwnd);
            if (lp & GCS_COMPSTR) {
                // 预编辑文本更新
                std::wstring preEdit = extractComposingText(m_himc);
                if (m_preEditCb) m_preEditCb(preEdit);
            }
            if (lp & GCS_RESULTSTR) {
                // 确认文本（用户选择了候选词）
                std::wstring committed = extractResultText(m_himc);
                if (m_commitCb) m_commitCb(committed);
            }
            if (m_himc) ImmReleaseContext(m_hwnd, m_himc);
            return true;
        }
        case WM_IME_ENDCOMPOSITION: {
            m_active = false;
            if (m_endCb) m_endCb();
            return true;
        }
    }
    return false;
}

std::wstring IMEWrapper::extractComposingText(HIMC himc) {
    if (!himc) return L"";
    LONG len = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
    if (len <= 0) return L"";
    std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
    ImmGetCompositionStringW(himc, GCS_COMPSTR, buf.data(), len);
    return std::wstring(buf.data(), len / sizeof(wchar_t));
}

std::wstring IMEWrapper::extractResultText(HIMC himc) {
    if (!himc) return L"";
    LONG len = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
    if (len <= 0) return L"";
    std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
    ImmGetCompositionStringW(himc, GCS_RESULTSTR, buf.data(), len);
    return std::wstring(buf.data(), len / sizeof(wchar_t));
}

void IMEWrapper::setCompositionWindowPos(const glm::vec2& screenPos) {
    m_himc = ImmGetContext(m_hwnd);
    if (!m_himc) return;
    COMPOSITIONFORM cf = {};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = (LONG)screenPos.x;
    cf.ptCurrentPos.y = (LONG)screenPos.y;
    ImmSetCompositionWindow(m_himc, &cf);
    ImmReleaseContext(m_hwnd, m_himc);
}

} // namespace fuzzel
```

---

### 5.15 FileWatcher 文件监控

> **v5 关键修复：** 补充 IOCP 初始化、多 watch 统一端口、重新投递逻辑。

```cpp
// src/system/FileWatcher.hpp
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include <functional>
#include <unordered_map>

namespace fuzzel {

struct FileChange {
    enum Type { Added, Removed, Modified, Renamed };
    Type type;
    std::wstring path;
};

class FileWatcher {
public:
    using ChangeCallback = std::function<void(const std::vector<FileChange>&)>;

    FileWatcher();
    ~FileWatcher();

    bool addWatch(const std::wstring& directory);
    void removeWatch(const std::wstring& directory);

    void start();
    void stop();

    // 主线程每帧调用：获取并处理累积的变更通知
    void processChanges();

    void setChangeCallback(ChangeCallback cb) { m_cb = std::move(cb); }

private:
    struct Watch {
        HANDLE hDir = INVALID_HANDLE_VALUE;
        std::wstring path;
        HANDLE hIoPort = nullptr;       // IOCP 句柄
        OVERLAPPED overlapped;
        BYTE buffer[4096];              // 目录变更通知缓冲区
        bool pending = false;           // 是否有待完成的异步读
    };

    std::vector<Watch> m_watches;
    std::thread m_worker;
    std::atomic<bool> m_running{false};

    // 统一 IOCP 端口（所有 watch 共享一个端口）
    HANDLE m_ioPort = nullptr;

    // 变更队列（工作线程 → 主线程）
    std::mutex m_queueMutex;
    std::deque<FileChange> m_changeQueue;

    ChangeCallback m_cb;

    void workerLoop();
    bool armWatch(Watch& watch);        // 投递 ReadDirectoryChangesW
    void processNotification(Watch& watch, DWORD bytesReturned);
};

} // namespace fuzzel
```

**完整实现：**

```cpp
// src/system/FileWatcher.cpp
#include "system/FileWatcher.hpp"
#include <algorithm>

namespace fuzzel {

// IOCP 自定义 key，用于标识 Watch
static constexpr ULONG_PTR WATCH_KEY = 0x1;

FileWatcher::FileWatcher() {}
FileWatcher::~FileWatcher() { stop(); }

bool FileWatcher::addWatch(const std::wstring& directory) {
    HANDLE hDir = CreateFileW(
        directory.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,  // 必须加 OVERLAPPED
        nullptr);

    if (hDir == INVALID_HANDLE_VALUE) return false;

    Watch watch;
    watch.hDir = hDir;
    watch.path = directory;

    // 创建统一 IOCP 端口（首次 addWatch 时）
    if (!m_ioPort) {
        m_ioPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (!m_ioPort) {
            CloseHandle(hDir);
            return false;
        }
    }

    // 将目录句柄关联到 IOCP 端口
    CreateIoCompletionPort(hDir, m_ioPort, WATCH_KEY, 0);

    m_watches.push_back(std::move(watch));

    // 立即投递第一次 ReadDirectoryChangesW
    armWatch(m_watches.back());

    return true;
}

bool FileWatcher::armWatch(Watch& watch) {
    memset(&watch.overlapped, 0, sizeof(OVERLAPPED));
    watch.overlapped.hEvent = nullptr;  // 使用 IOCP，不需要事件对象

    DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME |
                   FILE_NOTIFY_CHANGE_DIR_NAME |
                   FILE_NOTIFY_CHANGE_LAST_WRITE;

    BOOL ok = ReadDirectoryChangesW(
        watch.hDir,
        watch.buffer,
        sizeof(watch.buffer),
        TRUE,   // 递归监控子目录
        filter,
        nullptr,  // 不使用回调（返回字节数由 IOCP 提供）
        &watch.overlapped,
        nullptr   // 不使用 completion routine，使用 IOCP
    );

    watch.pending = ok;
    return ok;
}

void FileWatcher::start() {
    if (m_running) return;
    m_running = true;
    m_worker = std::thread(&FileWatcher::workerLoop, this);
}

void FileWatcher::stop() {
    if (!m_running) return;
    m_running = false;

    // 向 IOCP 发送退出信号
    PostQueuedCompletionStatus(m_ioPort, 0, 0, nullptr);

    if (m_worker.joinable()) m_worker.join();

    // 关闭所有目录句柄
    for (auto& w : m_watches) {
        if (w.hDir != INVALID_HANDLE_VALUE) CloseHandle(w.hDir);
    }
    m_watches.clear();

    if (m_ioPort) {
        CloseHandle(m_ioPort);
        m_ioPort = nullptr;
    }
}

void FileWatcher::workerLoop() {
    while (m_running) {
        DWORD bytesReturned = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED overlapped = nullptr;

        // 阻塞等待 IOCP 通知
        BOOL ok = GetQueuedCompletionStatus(
            m_ioPort, &bytesReturned, &key, &overlapped, INFINITE);

        if (!m_running) break;  // stop() 发送的退出信号

        if (!ok || bytesReturned == 0) {
            // 错误或目录句柄被关闭
            continue;
        }

        // 找到对应的 Watch
        for (auto& w : m_watches) {
            if (&w.overlapped == overlapped) {
                processNotification(w, bytesReturned);
                // 重新投递！否则后续变更不会被监控
                armWatch(w);
                break;
            }
        }
    }
}

void FileWatcher::processNotification(Watch& watch, DWORD bytesReturned) {
    BYTE* ptr = watch.buffer;
    DWORD remaining = bytesReturned;

    std::vector<FileChange> changes;

    while (remaining > 0) {
        auto* info = (FILE_NOTIFY_INFORMATION*)ptr;

        FileChange change;
        change.path = watch.path + L"\\" +
            std::wstring(info->FileName, info->FileNameLength / sizeof(wchar_t));

        switch (info->Action) {
            case FILE_ACTION_ADDED:          change.type = FileChange::Added; break;
            case FILE_ACTION_REMOVED:        change.type = FileChange::Removed; break;
            case FILE_ACTION_MODIFIED:       change.type = FileChange::Modified; break;
            case FILE_ACTION_RENAMED_OLD_NAME: change.type = FileChange::Renamed; break;
            case FILE_ACTION_RENAMED_NEW_NAME: change.type = FileChange::Added; break;
            default: break;
        }

        changes.push_back(change);

        if (info->NextEntryOffset == 0) break;
        ptr += info->NextEntryOffset;
        remaining -= info->NextEntryOffset;
    }

    // 推入队列，主线程处理
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        for (auto& c : changes) m_changeQueue.push_back(c);
    }
}

void FileWatcher::processChanges() {
    std::vector<FileChange> changes;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_changeQueue.empty()) return;
        changes.assign(m_changeQueue.begin(), m_changeQueue.end());
        m_changeQueue.clear();
    }
    if (m_cb && !changes.empty()) m_cb(changes);
}

} // namespace fuzzel
```

---

### 5.16 Logger 日志系统

```cpp
// src/system/Logger.hpp
#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <cstdint>

namespace fuzzel {

enum class LogLevel { Debug, Info, Warn, Error };

class Logger {
public:
    static void init(const std::string& dir);
    static void shutdown();

    static void log(LogLevel level, const std::string& msg);

    // fmt 格式化接口
    template <typename... Args>
    static void debugFmt(const char* fmt, Args&&... args);
    template <typename... Args>
    static void infoFmt(const char* fmt, Args&&... args);
    template <typename... Args>
    static void warnFmt(const char* fmt, Args&&... args);
    template <typename... Args>
    static void errorFmt(const char* fmt, Args&&... args);

    static void setLevel(LogLevel level) { s_level = level; }

    // 公开：路径展开工具函数
    static std::string expandEnv(const std::string& varName);

private:
    static LogLevel s_level;
    static std::string s_logDir;
    static std::ofstream s_file;
    static std::mutex s_mutex;
    static std::string s_currentDate;  // 当前日志文件对应的日期

    static void checkRotation();
    static std::string currentLogFile();
    static void openNewFile();
    static void cleanOldLogs();
};

} // namespace fuzzel
```

**路径展开：**

```cpp
std::string Logger::expandEnv(const std::string& varName) {
    char buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableA(varName.c_str(), buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return "";
    return std::string(buf, len);
}

// main.cpp 中：
Logger::init(Logger::expandEnv("APPDATA") + "/FuzzelGL/logs");
```

`expandEnv` 为 `public`，可被外部调用。

---

### 5.17 IndexDatabase 数据库

```cpp
// src/data/IndexDatabase.hpp
#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

namespace fuzzel {

struct AppRecord {
    int64_t id = 0;
    std::wstring name;
    std::wstring path;
    std::wstring arguments;
    std::wstring workingDir;
    std::wstring alias;
    std::wstring pinyin;
    std::wstring iconCacheKey;
    std::wstring appUserModelId;  // UWP 应用标识
    int launchCount = 0;
    int64_t lastLaunched = 0;
    bool isUWP = false;
};

struct SearchResultEntry {
    int64_t id;
    std::wstring name;
    std::wstring path;
    std::wstring iconCacheKey;
    std::wstring appUserModelId;
    bool isUWP;
    double score;  // 最终排序分数
};

class IndexDatabase {
public:
    IndexDatabase(const std::string& dbPath);
    ~IndexDatabase();

    bool init();
    void enableWAL();

    // 搜索（主线程，只读连接）
    // 返回前 100 条（bm25 排序），C++ 层再重排取前 20
    std::vector<SearchResultEntry> search(const std::string& query, int limit = 100);

    // 更新启动记录（后台写线程）
    void updateLaunchStats(int64_t appId);

    // CRUD（后台写线程）
    int64_t upsertApp(const AppRecord& record);
    void deleteApp(int64_t id);

    // 获取启动频次和时间戳（用于 C++ 重排）
    bool getLaunchStats(int64_t appId, int& count, int64_t& lastLaunched);

    // 完整性检查
    bool integrityCheck();

private:
    sqlite3* m_readDb = nullptr;   // 主线程只读连接
    sqlite3* m_writeDb = nullptr;  // 后台写连接
    std::string m_dbPath;
    std::mutex m_writeMutex;

    bool createTables();
    bool createFts();
    bool createTriggers();
};

} // namespace fuzzel
```

**SQL Schema:**

```sql
-- 主表
CREATE TABLE IF NOT EXISTS apps (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    path TEXT NOT NULL,
    arguments TEXT DEFAULT '',
    working_dir TEXT DEFAULT '',
    alias TEXT DEFAULT '',
    pinyin TEXT DEFAULT '',
    icon_cache_key TEXT DEFAULT '',
    app_user_model_id TEXT DEFAULT '',
    is_uwp INTEGER DEFAULT 0,
    launch_count INTEGER DEFAULT 0,
    last_launched INTEGER DEFAULT 0
);

-- FTS5 虚拟表（列权重：name=10, path=1, source=1, alias=5, pinyin=5）
CREATE VIRTUAL TABLE IF NOT EXISTS apps_fts USING fts5(
    name, path, source, alias, pinyin,
    content='apps',
    content_rowid='id'
);

-- 列权重通过查询时的 bm25() 函数指定：
-- SELECT ..., bm25(apps_fts, 10.0, 1.0, 1.0, 5.0, 5.0) AS score
--   FROM apps_fts WHERE apps_fts MATCH ?
--   ORDER BY score ASC LIMIT 100

-- 触发器自动同步
CREATE TRIGGER IF NOT EXISTS apps_ai AFTER INSERT ON apps BEGIN
    INSERT INTO apps_fts(rowid, name, path, source, alias, pinyin)
    VALUES (new.id, new.name, new.path, new.path, new.alias, new.pinyin);
END;

CREATE TRIGGER IF NOT EXISTS apps_ad AFTER DELETE ON apps BEGIN
    INSERT INTO apps_fts(apps_fts, rowid, name, path, source, alias, pinyin)
    VALUES ('delete', old.id, old.name, old.path, old.path, old.alias, old.pinyin);
END;

CREATE TRIGGER IF NOT EXISTS apps_au AFTER UPDATE ON apps BEGIN
    INSERT INTO apps_fts(apps_fts, rowid, name, path, source, alias, pinyin)
    VALUES ('delete', old.id, old.name, old.path, old.path, old.alias, old.pinyin);
    INSERT INTO apps_fts(rowid, name, path, source, alias, pinyin)
    VALUES (new.id, new.name, new.path, new.path, new.alias, new.pinyin);
END;
```

**搜索查询：**

```cpp
std::vector<SearchResultEntry> IndexDatabase::search(const std::string& query, int limit) {
    // SQL 取前 100 条（bm25 排序）
    const char* sql =
        "SELECT a.id, a.name, a.path, a.icon_cache_key, "
        "       a.app_user_model_id, a.is_uwp, "
        "       a.launch_count, a.last_launched, "
        "       bm25(apps_fts, 10.0, 1.0, 1.0, 5.0, 5.0) AS score "
        "FROM apps_fts f "
        "JOIN apps a ON a.id = f.rowid "
        "WHERE apps_fts MATCH ? "
        "ORDER BY score ASC "
        "LIMIT ?";

    // ...执行查询，返回 100 条结果
    // C++ 层调用 computeFinalScore 重排后取前 20
}
```

---

### 5.18 ConfigManager 配置管理

```cpp
// src/data/ConfigManager.hpp
#pragma once
#include <string>
#include <vector>
#include <functional>
#include <variant>
#include <cstdint>

namespace fuzzel {

struct HotkeyConfig {
    uint32_t modifiers = 0x0001;  // MOD_ALT
    uint32_t vk = 0x20;           // VK_SPACE
};

struct ThemeConfig {
    float bgColor[4] = {0.1f, 0.1f, 0.12f, 0.92f};
    float accentColor[4] = {0.3f, 0.6f, 1.0f, 1.0f};
    float cornerRadius = 8.0f;
    float shadowIntensity = 0.5f;
};

struct AliasEntry {
    std::wstring key;
    std::wstring value;
};

struct CustomCommand;  // 定义在 §2.2

struct ConfigChangeEvent {
    enum Type {
        HotkeyChanged,
        SearchDirsChanged,
        ThemeChanged,
        AliasAdded,       // data: AliasEntry
        AliasRemoved,     // data: std::wstring (key)
        CustomCommandsChanged  // 全量重载
    };
    Type type;
    std::variant<std::monostate, std::string, ThemeConfig,
                 std::vector<std::string>, AliasEntry, std::wstring,
                 std::vector<CustomCommand>> data;
};

class ConfigManager {
public:
    ConfigManager(const std::string& configPath);
    ~ConfigManager();

    bool load();
    bool save();
    bool saveBak();

    // 损坏恢复：尝试加载 → 失败则加载 .bak → 再失败则用默认配置
    bool loadWithFallback();

    const HotkeyConfig& getHotkey() const;
    const std::vector<std::string>& getSearchDirs() const;
    const ThemeConfig& getTheme() const;
    const std::vector<AliasEntry>& getAliases() const;
    const std::vector<CustomCommand>& getCustomCommands() const;

    void setHotkey(const HotkeyConfig& h);
    void addSearchDir(const std::string& dir);
    void removeSearchDir(const std::string& dir);
    void setTheme(const ThemeConfig& t);
    void addAlias(const std::wstring& key, const std::wstring& value);
    void removeAlias(const std::wstring& key);
    void setCustomCommands(const std::vector<CustomCommand>& cmds);

    using Observer = std::function<void(const ConfigChangeEvent&)>;
    void addObserver(Observer obs);
    void notifyObservers(const ConfigChangeEvent& evt);

private:
    std::string m_path;
    HotkeyConfig m_hotkey;
    std::vector<std::string> m_searchDirs;
    ThemeConfig m_theme;
    std::vector<AliasEntry> m_aliases;
    std::vector<CustomCommand> m_customCommands;
    std::vector<Observer> m_observers;
};

} // namespace fuzzel
```

---

### 5.19 PinyinConverter 拼音转换

> **v5：** 纯实例类（非单例），由 Application 管理。支持多音字。

```cpp
// src/data/PinyinConverter.hpp
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace fuzzel {

class PinyinConverter {
public:
    PinyinConverter();
    ~PinyinConverter();

    /// 加载拼音字典（从 third_party/mini-pinyin/）
    bool loadDictionary(const std::string& dictPath);

    /// 将中文字符串转为拼音首字母串。
    /// 多音字处理：每个汉字的所有读音首字母用逗号分隔。
    /// 例："长大" → "c,z,d" （"长" → "c,z"，"大" → "d"）
    std::string toFirstLetters(const std::wstring& text) const;

    /// 用户输入匹配检查。
    /// input: 用户输入的字母串（如 "zd"）
    /// pinyinStr: toFirstLetters 的返回值（如 "c,z,d"）
    /// 匹配规则：
    ///   1. 将 pinyinStr 按逗号分割为每组首字母列表：
    ///      "c,z,d" → [["c","z"], ["d"]]
    ///   2. 用户输入按字符逐组匹配：
    ///      "z" → 第 1 组包含 "z" ✓
    ///      "zd" → 第 1 组包含 "z" ✓，第 2 组包含 "d" ✓
    ///      "cd" → 第 1 组包含 "c" ✓，第 2 组包含 "d" ✓
    ///      "cz" → 第 1 组需匹配 "c" 和 "z"，但只有 1 个位置 → ✗
    ///   3. 每组最多消耗用户输入中 1 个字符
    static bool matchPinyin(const std::string& input, const std::string& pinyinStr);

private:
    // codepoint → 所有可能的拼音首字母
    std::unordered_map<uint32_t, std::vector<char>> m_dict;
};

} // namespace fuzzel
```

**匹配算法精确定义：**

```
输入：input = "zd"，pinyinStr = "c,z,d"

Step 1: 分割 pinyinStr
  groups = [["c","z"], ["d"]]

Step 2: 逐组匹配 input 的字符
  inputIdx = 0
  for each group:
    if inputIdx >= len(input): break  // input 已用完
    ch = input[inputIdx]
    if ch in group:
      inputIdx++        // 消耗一个字符
      // 继续下一组
    else:
      // 此组不消耗字符，继续下一组（允许跳过组）
      continue

Step 3: 检查 input 是否完全匹配
  return inputIdx == len(input)

例：input="z", groups=[["c","z"],["d"]]
  → group[0]: "z" in ["c","z"] ✓, inputIdx=1
  → group[1]: inputIdx >= len(input), break
  → inputIdx==1==len(input), return true

例：input="zd", groups=[["c","z"],["d"]]
  → group[0]: "z" in ["c","z"] ✓, inputIdx=1
  → group[1]: "d" in ["d"] ✓, inputIdx=2
  → inputIdx==2==len(input), return true

例：input="cd", groups=[["c","z"],["d"]]
  → group[0]: "c" in ["c","z"] ✓, inputIdx=1
  → group[1]: "d" in ["d"] ✓, inputIdx=2
  → return true

例：input="cz", groups=[["c","z"],["d"]]
  → group[0]: "c" in ["c","z"] ✓, inputIdx=1
  → group[1]: "z" in ["d"] ✗, continue
  → no more groups, inputIdx=1 != 2
  → return false
```

**mini-pinyin 依赖来源：** vendor 在 `third_party/mini-pinyin/`，非 vcpkg 包。包含 `pinyin_data.h`（字典数据）和 `pinyin.h`（接口）。`PinyinConverter::loadDictionary()` 读取此数据。CMake 中：

```cmake
if(ENABLE_PINYIN)
    target_include_directories(FuzzelGL PRIVATE third_party/mini-pinyin)
    target_sources(FuzzelGL PRIVATE
        src/data/PinyinConverter.cpp
        third_party/mini-pinyin/pinyin.c)
endif()
```

---

### 5.20 QueryCache 查询缓存

> **v5 更名：** "查询结果缓存"（非"前缀树"），实际实现就是 hash map。

```cpp
// src/data/QueryCache.hpp
#pragma once
#include "data/IndexDatabase.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <set>
#include <mutex>

namespace fuzzel {

class QueryCache {
public:
    QueryCache(IndexDatabase* db, size_t maxEntries = 200,
               std::chrono::seconds ttl = std::chrono::seconds(300));

    // 查询：先查缓存，miss 则查数据库
    std::vector<SearchResultEntry> query(const std::string& queryString);

    // 增量失效：指定 appId 的变更
    void invalidateApp(int64_t appId);

    // 大量变更时直接全量清空
    void invalidateAll();

private:
    IndexDatabase* m_db;
    size_t m_maxEntries;
    std::chrono::seconds m_ttl;

    struct CacheEntry {
        std::vector<SearchResultEntry> results;
        std::chrono::steady_clock::time_point createdAt;
        std::set<int64_t> appIds;  // 此结果包含的 appId 集合（用于增量失效）
    };

    std::unordered_map<std::string, CacheEntry> m_cache;
    std::mutex m_mutex;

    void evictExpired();
};

} // namespace fuzzel
```

**增量失效策略：**

- 少量变更（1~5 个 appId）：调用 `invalidateApp(appId)`，遍历缓存条目，从 `appIds` 集合中移除包含该 appId 的条目。
- 大量变更（>5 个 appId）：直接 `invalidateAll()`，全量清空。
- `AppScanner` 在批量变更时选择全量清空，单个 `.lnk` 变更时选择增量失效。

---

### 5.21 AppScanner 应用扫描

```cpp
// src/data/AppScanner.hpp
#pragma once
#include "data/IndexDatabase.hpp"
#include "data/PinyinConverter.hpp"
#include "data/LnkResolver.hpp"
#include "system/FileWatcher.hpp"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <functional>

namespace fuzzel {

struct ScanResult {
    int totalScanned = 0;
    int newApps = 0;
    int updated = 0;
};

class AppScanner {
public:
    AppScanner(IndexDatabase* db, PinyinConverter* pinyin, LnkResolver* lnk);
    ~AppScanner();

    void setSearchDirectories(const std::vector<std::string>& dirs);
    void startBackgroundScan();
    void stop();

    // 主线程每帧调用
    void processPendingChanges();

    // FileWatcher 回调
    void onFileChanges(const std::vector<FileChange>& changes);

    // 扫描状态
    bool isScanning() const { return m_scanning; }
    float getProgress() const { return m_progress; }

    // 扫描完成回调
    using ScanCompleteCallback = std::function<void(const ScanResult&)>;
    void setScanCompleteCallback(ScanCompleteCallback cb) { m_completeCb = std::move(cb); }

private:
    IndexDatabase* m_db;
    PinyinConverter* m_pinyin;
    LnkResolver* m_lnk;

    std::vector<std::string> m_searchDirs;
    std::thread m_scanThread;
    std::atomic<bool> m_scanning{false};
    std::atomic<float> m_progress{0.0f};

    // 目录黑名单
    const std::vector<std::wstring> m_blacklist = {
        L"C:\\Windows\\System32",
        L"C:\\Windows\\SysWOW64",
        L"C:\\Windows\\WinSxS"
    };

    // 符号链接深度限制
    static constexpr int MAX_SYMLINK_DEPTH = 3;

    // 待处理变更队列
    std::mutex m_changeMutex;
    std::queue<FileChange> m_pendingChanges;

    ScanCompleteCallback m_completeCb;

    void scanWorker();
    void scanDirectory(const std::wstring& dir, int depth, ScanResult& result);
    AppRecord extractAppInfo(const std::wstring& path);
    void scanUWPApps(ScanResult& result);  // UWP 应用枚举
};

} // namespace fuzzel
```

**`extractAppInfo` 实现逻辑：**

```
extractAppInfo(path):
  1. 判断扩展名：
     - ".exe" → 从 VersionInfo 提取 FileDescription 作为 name
     - ".lnk" → 调用 LnkResolver::resolve() 获取目标路径、参数、工作目录
       - 若目标是 UWP（通过 IPropertyStore 获取 AUMID）→ 设 isUWP=true
       - 否则 → 从目标 exe 的 VersionInfo 提取名称
     - 其他 → 跳过

  2. 生成 pinyin = PinyinConverter::toFirstLetters(name)

  3. 生成 iconCacheKey = path（用于 IconCache 查找）

  4. 构造 AppRecord {
       name, path, arguments, workingDir,
       pinyin, iconCacheKey,
       appUserModelId (UWP only),
       isUWP
     }
```

**UWP 应用枚举：**

```cpp
void AppScanner::scanUWPApps(ScanResult& result) {
    // 通过 PackageManager 枚举已安装 UWP 应用
    // 需要在 main.cpp 中初始化 COM（COINIT_APARTMENTTHREADED）
    using namespace Windows::Management::Deployment;
    auto manager = ref new PackageManager();
    auto packages = manager->FindPackagesForUser("");

    for (auto package : packages) {
        // 获取包的 AppUserModelId
        auto appId = getAppUserModelId(package);
        if (appId.empty()) continue;

        AppRecord record;
        record.name = package->Name->Data();
        record.appUserModelId = appId;
        record.isUWP = true;
        record.pinyin = m_pinyin->toFirstLetters(record.name);
        record.iconCacheKey = appId;  // UWP 图标通过 AUMID 获取

        m_db->upsertApp(record);
        result.newApps++;
    }
}
```

**扫描策略：**
- 单线程扫描（避免数据库写入竞争）
- 先扫描开始菜单（快速就绪），再扫描桌面和自定义目录
- 跳过黑名单目录（`System32` 等）
- 符号链接跟踪深度限制 3 层

---

### 5.22 LnkResolver 快捷方式解析

```cpp
// src/data/LnkResolver.hpp
#pragma once
#include <string>
#include <windows.h>
#include <shlobj.h>
#include <propkey.h>

namespace fuzzel {

struct LnkInfo {
    std::wstring targetPath;
    std::wstring arguments;
    std::wstring workingDir;
    std::wstring iconPath;
    int iconIndex = 0;
    std::wstring appUserModelId;  // UWP 应用的 AUMID
    bool isUWP = false;
};

class LnkResolver {
public:
    LnkResolver();
    ~LnkResolver();

    /// 解析 .lnk 快捷方式。调用前需保证 COM 已初始化。
    bool resolve(const std::wstring& lnkPath, LnkInfo& outInfo);

private:
    /// 从 IShellLink 的 IPropertyStore 提取 AUMID
    bool extractAumId(IShellLinkW* shellLink, std::wstring& outAumId);
};

} // namespace fuzzel
```

**实现：**

```cpp
// src/data/LnkResolver.cpp
#include "data/LnkResolver.hpp"
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace fuzzel {

LnkResolver::LnkResolver() {}
LnkResolver::~LnkResolver() {}

bool LnkResolver::resolve(const std::wstring& lnkPath, LnkInfo& outInfo) {
    // 注意：调用者必须保证 CoInitializeEx 已调用（在 main.cpp 中统一初始化）
    ComPtr<IShellLinkW> shellLink;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&shellLink));
    if (FAILED(hr)) return false;

    ComPtr<IPersistFile> persistFile;
    hr = shellLink.As(&persistFile);
    if (FAILED(hr)) return false;

    hr = persistFile->Load(lnkPath.c_str(), STGM_READ);
    if (FAILED(hr)) return false;

    // 提取目标路径、参数、工作目录
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
    outInfo.workDir = workDir;
    outInfo.iconPath = iconPath;
    outInfo.iconIndex = iconIdx;

    // 检查是否为 UWP 应用（通过 IPropertyStore 获取 AUMID）
    extractAumId(shellLink.Get(), outInfo.appUserModelId);
    outInfo.isUWP = !outInfo.appUserModelId.empty();

    return true;
}

bool LnkResolver::extractAumId(IShellLinkW* shellLink, std::wstring& outAumId) {
    ComPtr<IPropertyStore> propStore;
    HRESULT hr = shellLink->QueryInterface(IID_PPV_ARGS(&propStore));
    if (FAILED(hr)) return false;

    PROPVARIANT pv;
    PropVariantInit(&pv);
    hr = propStore->GetValue(PKEY_AppUserModel_ID, &pv);
    if (SUCCEEDED(hr) && pv.vt == VT_LPWSTR) {
        outAumId = pv.pwszVal;
    }
    PropVariantClear(&pv);
    return !outAumId.empty();
}

} // namespace fuzzel
```

> **LnkResolver 不负责 COM 初始化。** 调用者（`main.cpp`）保证 `CoInitializeEx(COINIT_APARTMENTTHREADED)` 已调用。

---

### 5.23 AppLauncher 应用启动

```cpp
// src/app/AppLauncher.hpp
#pragma once
#include "data/IndexDatabase.hpp"
#include "data/ConfigManager.hpp"
#include <string>
#include <functional>

namespace fuzzel {

class AppLauncher {
public:
    AppLauncher();
    ~AppLauncher();

    /// 启动应用。调用前需保证 COM 已初始化。
    bool launch(const SearchResultEntry& entry);

    /// 启动自定义命令
    bool launchCustomCommand(const CustomCommand& cmd);

    using LaunchCompleteCallback = std::function<void(bool success)>;
    void setLaunchCompleteCallback(LaunchCompleteCallback cb) { m_cb = std::move(cb); }

private:
    LaunchCompleteCallback m_cb;

    bool launchExe(const std::wstring& path, const std::wstring& args,
                   const std::wstring& workDir, bool runAsAdmin);
    bool launchUWP(const std::wstring& appUserModelId);
    bool launchSystemAPI(CustomCommand::SystemAction action);
    bool launchScript(const std::wstring& scriptPath);
};

} // namespace fuzzel
```

> **AppLauncher 不负责 COM 初始化。** 调用者保证 COM 已调用。

---

### 5.24 ConfigPage 配置面板

> **v5：** 内嵌 GLFW 页面，不是 Win32 对话框。

```cpp
// src/ui/ConfigPage.hpp
#pragma once
#include "ui/Widget.hpp"
#include "render/CommandBuffer.hpp"
#include "data/ConfigManager.hpp"
#include <vector>
#include <string>
#include <glm/glm.hpp>

namespace fuzzel {

class ConfigPage : public Widget {
public:
    ConfigPage(FontManager* fontMgr, ConfigManager* config);
    ~ConfigPage();

    void paint(CommandBuffer& cmdBuf) override;
    bool onMouseEvent(const MouseEvent& evt) override;
    bool onKeyEvent(const KeyEvent& evt) override;
    bool onCharEvent(const CharEvent& evt) override;

    void onShow();
    void onHide();

    // 热键捕获模式
    void startHotkeyCapture();
    bool isCapturingHotkey() const { return m_capturingHotkey; }

private:
    FontManager* m_fontMgr;
    ConfigManager* m_config;

    // 控件命中测试矩形（屏幕坐标）
    struct Button {
        glm::vec2 pos, size;
        std::wstring label;
        std::function<void()> onClick;
        bool hovered = false;
    };

    struct TextInput {
        glm::vec2 pos, size;
        std::wstring text;
        bool focused = false;
        int cursorPos = 0;
    };

    std::vector<Button> m_buttons;
    std::vector<TextInput> m_inputs;

    bool m_capturingHotkey = false;

    // 当前编辑状态
    ThemeConfig m_editingTheme;  // 编辑中的主题（未保存）
    std::vector<std::string> m_editingDirs;

    void rebuildLayout();
    Button* hitTestButton(const glm::vec2& pos);
    TextInput* hitTestInput(const glm::vec2& pos);
};

} // namespace fuzzel
```

---

### 5.25 阴影/模糊特效着色器

**双 Pass 高斯模糊：**

```glsl
// shaders/Blur.vert
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
out vec2 vUV;
uniform vec2 uTexelOffset;  // (1/width, 1/height)
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}

// shaders/Blur.frag
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform vec2 uDirection;  // 水平: (1,0), 垂直: (0,1)
uniform float uRadius;    // 模糊半径（像素）
void main() {
    vec2 texelSize = 1.0 / textureSize(uTexture, 0);
    vec2 dir = uDirection * texelSize * uRadius;

    // 9-tap 高斯近似
    vec4 sum = vec4(0.0);
    float weights[5] = float[](0.227027, 0.1945946, 0.1216216,
                                0.054054, 0.016216);
    sum += texture(uTexture, vUV) * weights[0];
    sum += texture(uTexture, vUV + dir) * weights[1];
    sum += texture(uTexture, vUV - dir) * weights[1];
    sum += texture(uTexture, vUV + 2.0 * dir) * weights[2];
    sum += texture(uTexture, vUV - 2.0 * dir) * weights[2];
    sum += texture(uTexture, vUV + 3.0 * dir) * weights[3];
    sum += texture(uTexture, vUV - 3.0 * dir) * weights[3];
    sum += texture(uTexture, vUV + 4.0 * dir) * weights[4];
    sum += texture(uTexture, vUV - 4.0 * dir) * weights[4];

    FragColor = sum;
}
```

**阴影合成：**

```glsl
// shaders/ShadowComposite.frag
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uShadowTexture;  // 模糊后的纹理
uniform sampler2D uSceneTexture;   // 原始场景
uniform vec4 uShadowColor;
uniform vec2 uOffset;
void main() {
    vec4 shadow = texture(uShadowTexture, vUV + uOffset);
    vec4 scene = texture(uSceneTexture, vUV);
    // 阴影 alpha 混合到场景下方
    float alpha = shadow.a * uShadowColor.a;
    vec3 color = mix(scene.rgb, uShadowColor.rgb, alpha);
    FragColor = vec4(color, scene.a);
}
```

**ShadowOp 执行流程：**

```
1. 后端收到 ShadowOp{sourceTexture, blurRadius, offset, color}
2. Pass 1: 分配临时纹理 texH，水平模糊 sourceTexture → texH
3. Pass 2: 分配临时纹理 texV，垂直模糊 texH → texV
4. Pass 3: 将 texV 以 color 着色、offset 偏移，混合到当前激活的 FBO
5. 临时纹理由 ResourceManager::recycleTemporaries() 在帧结束回收
```

---

### 5.26 CrashHandler 崩溃恢复

> **v5：** 自包含，不依赖 Logger。

```cpp
// src/app/CrashHandler.hpp
#pragma once
#include <windows.h>
#include <string>

namespace fuzzel {

class CrashHandler {
public:
    /// 安装崩溃处理器。在 main() 最开始调用。
    static void install();

    /// 写崩溃日志（自包含，不依赖 Logger）
    static void writeCrashLog(EXCEPTION_POINTERS* ep, const char* context);

private:
    static LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* ep);

    // 直接调用 Win32 API 获取 APPDATA 路径，不依赖 Logger::expandEnv
    static std::string getAppDataPath();
};

} // namespace fuzzel
```

**实现：**

```cpp
// src/app/CrashHandler.cpp
#include "app/CrashHandler.hpp"
#include <fstream>
#include <ctime>

namespace fuzzel {

void CrashHandler::install() {
    SetUnhandledExceptionFilter(&CrashHandler::unhandledExceptionFilter);
}

LONG WINAPI CrashHandler::unhandledExceptionFilter(EXCEPTION_POINTERS* ep) {
    writeCrashLog(ep, "Unhandled Exception");
    return EXCEPTION_EXECUTE_HANDLER;  // 终止进程
}

std::string CrashHandler::getAppDataPath() {
    char buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return ".";
    return std::string(buf, len);
}

void CrashHandler::writeCrashLog(EXCEPTION_POINTERS* ep, const char* context) {
    std::string dir = getAppDataPath() + "/FuzzelGL/logs";
    CreateDirectoryA(dir.c_str(), nullptr);

    time_t now = time(nullptr);
    tm lt;
    localtime_s(&lt, &now);
    char filename[256];
    strftime(filename, sizeof(filename),
             "%Y%m%d_%H%M%S_crash.log", &lt);

    std::ofstream f(dir + "/" + filename);
    if (!f) return;

    f << "=== FuzzelGL Crash Log ===\n";
    f << "Time: " << asctime(&lt);
    f << "Context: " << context << "\n";
    if (ep && ep->ExceptionRecord) {
        f << "Exception Code: 0x" << std::hex
          << ep->ExceptionRecord->ExceptionCode << "\n";
        f << "Exception Address: 0x" << std::hex
          << (uintptr_t)ep->ExceptionRecord->ExceptionAddress << "\n";
    }
    f.close();

    // 可选：显示崩溃对话框
    MessageBoxA(nullptr,
                "FuzzelGL 遇到致命错误并已崩溃。\n"
                "崩溃日志已写入 %APPDATA%/FuzzelGL/logs/",
                "FuzzelGL Crash", MB_OK | MB_ICONERROR);
}

} // namespace fuzzel
```

---

### 5.27 StringUtils 工具函数

```cpp
// src/utils/StringUtils.hpp
#pragma once
#include <string>

namespace fuzzel {

/// UTF-8 → UTF-16
std::wstring utf8ToWide(const std::string& str);

/// UTF-16 → UTF-8
std::string wideToUtf8(const std::wstring& str);

} // namespace fuzzel
```

**实现：**

```cpp
// src/utils/StringUtils.cpp
#include "utils/StringUtils.hpp"
#include <windows.h>

namespace fuzzel {

std::wstring utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
                                  (int)str.size(), nullptr, 0);
    std::wstring result(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
                        (int)str.size(), &result[0], len);
    return result;
}

std::string wideToUtf8(const std::wstring& str) {
    if (str.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, str.c_str(),
                                  (int)str.size(), nullptr, 0,
                                  nullptr, nullptr);
    std::string result(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, str.c_str(),
                        (int)str.size(), &result[0], len,
                        nullptr, nullptr);
    return result;
}

} // namespace fuzzel
```

---

## 6. 主循环与启动流程

```cpp
// src/app/Application.hpp
#pragma once
#include "system/WindowManager.hpp"
#include "system/HotkeyManager.hpp"
#include "system/TrayManager.hpp"
#include "system/IMEWrapper.hpp"
#include "system/FileWatcher.hpp"
#include "system/Logger.hpp"
#include "render/IRenderBackend.hpp"
#include "render/CommandBuffer.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/FontManager.hpp"
#include "resource/IconCache.hpp"
#include "ui/UIManager.hpp"
#include "data/IndexDatabase.hpp"
#include "data/ConfigManager.hpp"
#include "data/PinyinConverter.hpp"
#include "data/QueryCache.hpp"
#include "data/AppScanner.hpp"
#include "data/LnkResolver.hpp"
#include "app/AppLauncher.hpp"
#include "app/CrashHandler.hpp"
#include "utils/StringUtils.hpp"
#include <memory>
#include <chrono>
#include <cstdint>

namespace fuzzel {

class Application {
public:
    Application();
    ~Application();

    int run();

private:
    // 核心系统
    std::unique_ptr<ConfigManager>      m_config;
    std::unique_ptr<WindowManager>      m_winMgr;
    std::unique_ptr<IRenderBackend>     m_backend;
    std::unique_ptr<ResourceManager>    m_resMgr;
    std::unique_ptr<FontManager>        m_fontMgr;
    std::unique_ptr<IconCache>          m_iconCache;
    std::unique_ptr<UIManager>          m_uiMgr;

    // 数据层
    std::unique_ptr<IndexDatabase>      m_db;
    std::unique_ptr<QueryCache>         m_queryCache;
    std::unique_ptr<PinyinConverter>    m_pinyin;
    std::unique_ptr<LnkResolver>        m_lnkResolver;
    std::unique_ptr<AppScanner>         m_scanner;
    std::unique_ptr<AppLauncher>        m_launcher;

    // 系统服务
    std::unique_ptr<HotkeyManager>      m_hotkeyMgr;
    std::unique_ptr<TrayManager>        m_trayMgr;
    std::unique_ptr<IMEWrapper>         m_ime;
    std::unique_ptr<FileWatcher>        m_fileWatcher;

    // 状态
    bool m_uiDirty = false;
    uint64_t m_frameIndex = 0;
    bool m_running = false;

    // 搜索防抖
    std::chrono::steady_clock::time_point m_lastInputTime;
    bool m_searchPending = false;
    std::wstring m_pendingQuery;

    // 方法
    bool init();
    void shutdown();
    void handleWin32Messages();
    void onKey(int key, int scancode, int action, int mods);
    void onChar(uint32_t codepoint);
    void onMouse(int button, int action, int mods);
    void onCursor(double x, double y);
    void triggerSearch(const std::wstring& query);
    void onSearch(const std::wstring& query);
    void onLaunch(const SearchResultEntry& entry);
    void handleConfigChange(const ConfigChangeEvent& evt);
    void onDpiChanged(int newDpi);
};

} // namespace fuzzel
```

**main.cpp:**

```cpp
// src/main.cpp
#include "app/Application.hpp"
#include "app/CrashHandler.hpp"
#include "system/Logger.hpp"
#include <windows.h>
#include <ole2.h>  // CoInitializeEx

// 单实例检查
static bool checkSingleInstance() {
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"FuzzelGL_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // 已有实例运行
        CloseHandle(hMutex);
        return false;
    }
    // hMutex 在进程退出时自动释放
    return true;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // 1. 崩溃处理器（最先安装）
    CrashHandler::install();

    // 2. 单实例检查
    if (!checkSingleInstance()) {
        MessageBoxW(nullptr, L"FuzzelGL 已在运行中。", L"FuzzelGL", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // 3. COM 初始化（COINIT_APARTMENTTHREADED，供 LnkResolver / UWP 使用）
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"COM 初始化失败。", L"FuzzelGL", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 4. Logger 初始化
    Logger::init(Logger::expandEnv("APPDATA") + "/FuzzelGL/logs");
    Logger::infoFmt("FuzzelGL starting up");

    // 5. 运行应用（异常保护）
    int exitCode = 0;
    try {
        Application app;
        exitCode = app.run();
    } catch (const std::exception& e) {
        Logger::errorFmt("Fatal: {}", e.what());
        CrashHandler::writeCrashLog(nullptr, e.what());
        exitCode = 1;
    } catch (...) {
        Logger::errorFmt("Fatal: unknown exception");
        CrashHandler::writeCrashLog(nullptr, "unknown exception");
        exitCode = 1;
    }

    // 6. 清理
    Logger::infoFmt("FuzzelGL shutting down, exit code {}", exitCode);
    Logger::shutdown();
    CoUninitialize();
    return exitCode;
}
```

**Application::run() 主循环：**

```cpp
int Application::run() {
    if (!init()) return 1;

    m_running = true;
    double lastTime = glfwGetTime();

    while (m_running && !m_winMgr->shouldClose()) {
        // 1. 处理 Win32 消息（热键、IME、托盘、对话框）
        handleWin32Messages();

        // 2. GLFW 事件（键盘、鼠标等）
        glfwPollEvents();

        // 3. 处理文件变更
        m_fileWatcher->processChanges();
        m_scanner->processPendingChanges();

        // 4. 处理图标异步加载结果（在主线程注册 GL 纹理）
        m_iconCache->processPending();

        // 5. 搜索防抖检查
        if (m_searchPending) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_lastInputTime);
            if (elapsed.count() >= 50) {
                m_searchPending = false;
                onSearch(m_pendingQuery);
            }
        }

        // 6. 更新动画
        double glfwNow = glfwGetTime();
        float delta = (float)(glfwNow - lastTime);
        lastTime = glfwNow;
        m_uiMgr->update(delta);

        // 7. 渲染（仅在窗口可见时）
        if (m_winMgr->isVisible()) {
            CommandBuffer cmdBuf;
            if (m_uiDirty || m_uiMgr->isDirty() || m_uiMgr->getAnimation().hasActive()) {
                m_uiMgr->paint(cmdBuf);
                m_uiDirty = false;
                m_uiMgr->clearDirty();
            }

            m_backend->beginFrame();
            if (!cmdBuf.getCommands().empty()) {
                m_backend->execute(cmdBuf);
            }
            m_backend->endFrame();
            m_backend->present();
        } else {
            // 窗口隐藏：低功耗模式，降低帧率
            Sleep(50);  // ~20fps 唤醒检查
        }

        // 8. 回收临时纹理和延迟资源
        m_resMgr->recycleTemporaries();
        m_backend->collectGarbage(m_frameIndex);
        m_frameIndex++;
    }

    shutdown();
    return 0;
}
```

**handleWin32Messages:**

```cpp
void Application::handleWin32Messages() {
    MSG msg;

    // 热键消息
    while (PeekMessage(&msg, nullptr, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
        if (m_hotkeyMgr && m_hotkeyMgr->checkMessage(msg)) {
            m_winMgr->toggleVisibility();
        }
    }

    // IME 消息（WM_IME_STARTCOMPOSITION=0x010D ~ WM_IME_COMPOSITION=0x010F）
    while (PeekMessage(&msg, nullptr, WM_IME_STARTCOMPOSITION, WM_IME_COMPOSITION, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 托盘消息（由 TrayManager 的独立窗口处理）
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (m_trayMgr && m_trayMgr->processMessage(msg)) continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}
```

**搜索防抖与 onSearch:**

```cpp
void Application::onKey(int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        // IME 激活时，仅 Esc 生效
        if (m_winMgr->isImeActive()) {
            if (key == GLFW_KEY_ESCAPE) {
                // 由 IMEWrapper 处理清空预编辑
                return;
            }
            return;  // 其他按键交给 IME
        }
        m_uiMgr->onKeyEvent({key, scancode, action, mods});
    }
}

void Application::onChar(uint32_t codepoint) {
    // IME 激活时忽略 GLFW 字符事件（避免重复输入）
    if (m_winMgr->isImeActive()) return;
    m_uiMgr->onCharEvent({codepoint});
}

void Application::triggerSearch(const std::wstring& query) {
    m_pendingQuery = query;
    m_lastInputTime = std::chrono::steady_clock::now();
    m_searchPending = true;
}

void Application::onSearch(const std::wstring& query) {
    auto results = m_queryCache->query(fuzzel::wideToUtf8(query));
    m_uiMgr->setSearchResults(results);
    m_uiDirty = true;
}
```

**init():**

```cpp
bool Application::init() {
    // 1. 加载配置
    m_config = std::make_unique<ConfigManager>("config.json");
    m_config->loadWithFallback();
    m_config->addObserver([this](const ConfigChangeEvent& evt) {
        handleConfigChange(evt);
    });

    // 2. 初始化窗口
    m_winMgr = std::make_unique<WindowManager>(800, 60, "FuzzelGL");
    if (!m_winMgr->init()) return false;
    m_winMgr->registerCallbacks();  // 注册 GLFW 回调
    m_winMgr->subclassWindow();     // 子类化窗口过程（DPI + IME）

    // 设置 GLFW 回调
    m_winMgr->setKeyCallback([this](int k, int s, int a, int m) { onKey(k, s, a, m); });
    m_winMgr->setCharCallback([this](uint32_t c) { onChar(c); });
    m_winMgr->setMouseCallback([this](int b, int a, int m) { onMouse(b, a, m); });
    m_winMgr->setCursorCallback([this](double x, double y) { onCursor(x, y); });

    // 3. 创建渲染后端（编译时选择）
#ifdef FUZZEL_USE_VULKAN
    m_backend = std::make_unique<VulkanBackend>();
#else
    m_backend = std::make_unique<OpenGLBackend>();
#endif
    if (!m_backend->init(m_winMgr->getGLFWWindow())) return false;

    // 4. 资源管理器
    m_resMgr = std::make_unique<ResourceManager>();
    m_resMgr->setBackend(m_backend.get());

    // 5. 字体
    m_fontMgr = std::make_unique<FontManager>(m_resMgr.get());
    m_fontMgr->loadFont(L"C:/Windows/Fonts/msyh.ttc", 18.0f * m_winMgr->getDpiScale());

    // 6. 图标缓存
    m_iconCache = std::make_unique<IconCache>(m_resMgr.get(), 256);
    m_iconCache->setInvalidateCallback([this]() { m_uiDirty = true; });

    // 7. UI 系统
    m_uiMgr = std::make_unique<UIManager>(
        m_fontMgr.get(), m_iconCache.get(), m_resMgr.get(), m_winMgr->getDpiScale());

    // 设置回调
    m_uiMgr->setSearchCallback([this](const std::wstring& q) { triggerSearch(q); });
    m_uiMgr->setLaunchCallback([this](const SearchResultEntry& e) { onLaunch(e); });

    // 8. 数据库
    std::string dbPath = Logger::expandEnv("APPDATA") + "/FuzzelGL/apps.db";
    m_db = std::make_unique<IndexDatabase>(dbPath);
    m_db->init();
    m_db->enableWAL();

    // 9. 拼音
    m_pinyin = std::make_unique<PinyinConverter>();
    m_pinyin->loadDictionary("third_party/mini-pinyin/pinyin_data.h");

    // 10. LnkResolver
    m_lnkResolver = std::make_unique<LnkResolver>();

    // 11. AppScanner
    m_scanner = std::make_unique<AppScanner>(
        m_db.get(), m_pinyin.get(), m_lnkResolver.get());
    m_scanner->setSearchDirectories(m_config->getSearchDirs());
    m_scanner->startBackgroundScan();

    // 12. QueryCache
    m_queryCache = std::make_unique<QueryCache>(m_db.get());

    // 13. AppLauncher
    m_launcher = std::make_unique<AppLauncher>();

    // 14. FileWatcher
    m_fileWatcher = std::make_unique<FileWatcher>();
    for (const auto& dir : m_config->getSearchDirs()) {
        m_fileWatcher->addWatch(fuzzel::utf8ToWide(dir));
    }
    m_fileWatcher->setChangeCallback(
        [this](const std::vector<FileChange>& changes) {
            m_scanner->onFileChanges(changes);
        });
    m_fileWatcher->start();

    // 15. 热键
    m_hotkeyMgr = std::make_unique<HotkeyManager>();
    m_hotkeyMgr->register(m_config->getHotkey());
    m_hotkeyMgr->setCallback([this]() {
        m_winMgr->toggleVisibility();
    });

    // 16. 托盘
    m_trayMgr = std::make_unique<TrayManager>();
    HICON hIcon = LoadIcon(nullptr, IDI_APPLICATION);  // TODO: 自定义图标
    m_trayMgr->init(hIcon, L"FuzzelGL");
    m_trayMgr->setShowCallback([this]() { m_winMgr->show(); });
    m_trayMgr->setConfigCallback([this]() { m_uiMgr->showConfigPage(); m_winMgr->show(); });
    m_trayMgr->setQuitCallback([this]() { m_running = false; });

    // 17. IME
    m_ime = std::make_unique<IMEWrapper>();
    m_ime->init(m_winMgr->getHWND());
    m_ime->setPreEditCallback([this](const std::wstring& preEdit) {
        // 更新预编辑文本到 InputField
        m_uiMgr->markDirty();
    });
    m_ime->setCommitCallback([this](const std::wstring& committed) {
        // 提交文本到 InputField
        m_uiMgr->markDirty();
    });
    m_ime->setEndCallback([this]() {
        m_winMgr->setImeActive(false);
        m_uiMgr->markDirty();
    });

    return true;
}
```

**shutdown():**

```cpp
void Application::shutdown() {
    m_running = false;

    // 停止后台线程（顺序很重要）
    m_fileWatcher->stop();       // 停止文件监控线程
    m_scanner->stop();           // 停止扫描线程

    // 保存配置
    if (m_config) m_config->save();

    // 释放系统服务
    m_trayMgr->remove();
    m_hotkeyMgr->unregister();

    // 释放 UI（先于资源，因为 UI 引用资源）
    m_uiMgr.reset();
    m_iconCache.reset();
    m_fontMgr.reset();

    // 释放资源管理器（注销所有纹理）
    m_resMgr.reset();

    // 释放后端（释放 GPU 资源）
    m_backend.reset();

    // 释放窗口
    m_winMgr->shutdown();

    // 关闭数据库
    m_db.reset();
}
```

---

## 7. 构建与部署

### 7.1 工具链与依赖

- 编译器：MSVC 2022（v143）
- 依赖库（通过 vcpkg 管理）：

| 库 | 版本 | 用途 |
|----|------|------|
| GLFW | 3.4 | 窗口与输入 |
| GLEW | 2.1 | OpenGL 函数加载 |
| FreeType | 2.13 | 字体光栅化 |
| sqlite3 | 3.42+ (fts5) | 搜索索引 |
| nlohmann-json | 3.11 | 配置文件 |
| fmt | 10.0 | 日志格式化 |
| glm | 1.0 | 数学库 |

**vendor 库（非 vcpkg）：**

| 库 | 位置 | 用途 |
|----|------|------|
| mini-pinyin | `third_party/mini-pinyin/` | 拼音字典 |

### 7.2 vcpkg.json

```json
{
  "name": "fuzzelgl",
  "version-string": "0.1.0",
  "dependencies": [
    "glfw3",
    {
      "name": "sqlite3",
      "features": ["fts5"]
    },
    "glew",
    "freetype",
    "nlohmann-json",
    "fmt",
    "glm"
  ]
}
```

> **注意：** sqlite3 只出现一次，带 `fts5` feature。不重复列出。

### 7.3 CMake 配置

```cmake
cmake_minimum_required(VERSION 3.20)
project(FuzzelGL VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Options ──────────────────────────────────────────────
option(USE_VULKAN "Use Vulkan backend instead of OpenGL" OFF)
option(ENABLE_PINYIN "Enable Pinyin search support" ON)
option(FUZZEL_BUILD_TESTS "Build unit tests" ON)

# ── MSVC specifics ──────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
    add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    # 动态链接 VC++ 运行时（与 ≤8MB 体积目标一致）
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# ── Output ──────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ── Manifest (PerMonitorV2 DPI) ─────────────────────────
set(MANIFEST_FILE ${CMAKE_SOURCE_DIR}/cmake/FuzzelGL.manifest)

# ── Sources ─────────────────────────────────────────────
set(FUZZEL_SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/app/AppLauncher.cpp
    src/app/CrashHandler.cpp
    src/ui/Widget.cpp
    src/ui/InputField.cpp
    src/ui/ResultList.cpp
    src/ui/ConfigPage.cpp
    src/ui/UIManager.cpp
    src/ui/LayoutEngine.cpp
    src/ui/AnimationEngine.cpp
    src/render/CommandBuffer.cpp
    src/render/ResourceManager.cpp
    src/resource/FontManager.cpp
    src/resource/IconCache.cpp
    src/system/WindowManager.cpp
    src/system/HotkeyManager.cpp
    src/system/TrayManager.cpp
    src/system/IMEWrapper.cpp
    src/system/FileWatcher.cpp
    src/system/Logger.cpp
    src/data/IndexDatabase.cpp
    src/data/ConfigManager.cpp
    src/data/PinyinConverter.cpp
    src/data/QueryCache.cpp
    src/data/AppScanner.cpp
    src/data/LnkResolver.cpp
    src/utils/StringUtils.cpp
)

# ── Target ──────────────────────────────────────────────
add_executable(FuzzelGL WIN32 ${FUZZEL_SOURCES})

target_include_directories(FuzzelGL PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/mini-pinyin
)

# ── Dependencies ────────────────────────────────────────
find_package(glfw3 CONFIG REQUIRED)
find_package(GLEW CONFIG REQUIRED)
find_package(freetype CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

target_link_libraries(FuzzelGL PRIVATE
    glfw
    GLEW::GLEW
    freetype
    unofficial::sqlite3::sqlite3
    nlohmann_json::nlohmann_json
    fmt::fmt
    glm
)

# ── Pinyin ──────────────────────────────────────────────
if(ENABLE_PINYIN)
    target_compile_definitions(FuzzelGL PRIVATE FUZZEL_ENABLE_PINYIN)
    target_sources(FuzzelGL PRIVATE
        third_party/mini-pinyin/pinyin.c)
endif()

# ── Backend ─────────────────────────────────────────────
if(USE_VULKAN)
    find_package(Vulkan REQUIRED)
    target_compile_definitions(FuzzelGL PRIVATE FUZZEL_USE_VULKAN)
    target_sources(FuzzelGL PRIVATE
        src/render/VulkanBackend.cpp
        src/render/VulkanBackend.hpp)
    target_link_libraries(FuzzelGL PRIVATE Vulkan::Vulkan)
else()
    find_package(OpenGL REQUIRED)
    target_compile_definitions(FuzzelGL PRIVATE FUZZEL_USE_OPENGL)
    target_sources(FuzzelGL PRIVATE
        src/render/OpenGLBackend.cpp
        src/render/OpenGLBackend.hpp)
    target_link_libraries(FuzzelGL PRIVATE OpenGL::GL)
endif()

# ── Linker flags ────────────────────────────────────────
if(MSVC)
    set_target_properties(FuzzelGL PROPERTIES
        LINK_FLAGS "/MANIFEST:EMBED /MANIFESTINPUT:${MANIFEST_FILE}"
    )
endif()

# ── Tests ───────────────────────────────────────────────
if(FUZZEL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

### 7.4 DPI 清单

```xml
<!-- cmake/FuzzelGL.manifest -->
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">
        PerMonitorV2
      </dpiAwareness>
      <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">
        true/pm
      </dpiAware>
    </windowsSettings>
  </application>
</assembly>
```

### 7.5 打包

使用 Inno Setup 制作安装包，或直接发布单个 `.exe` + `vcruntime140.dll`。

---

## 8. 测试与质量保证

| 测试类型 | 内容 |
|---------|------|
| 单元测试 | `FontManager` 字形生成、`PinyinConverter` 转换与匹配、`QueryCache` 缓存命中/失效、`ResourceManager` 句柄分配/回收、`StringUtils` 编码转换 |
| 集成测试 | 模拟热键呼出 → 输入搜索 → 选择 → 启动全流程 |
| 性能基准 | 5000/10000 条应用下搜索响应时间（<30ms），内存峰值（<45MB） |
| 高 DPI 测试 | 多显示器不同缩放比（100%/150%/200%）下窗口布局正确，拖拽跨显示器时 DPI 切换正确 |
| 数据库修复 | 手动损坏 `apps.db`，验证 WAL 自动恢复和 `integrity_check` |
| 崩溃测试 | 模拟空指针访问，验证 `CrashHandler` 生成崩溃日志 |
| 文件监控 | 在监控目录中增删 `.lnk` 文件，验证 `FileWatcher` 检测变更并增量更新索引 |

---

## 9. 里程碑路线图

### Phase 1：OpenGL 全链路（P0 核心）

| 优先级 | 模块 | 交付物 |
|--------|------|--------|
| P0 | CMake + vcpkg | 项目可编译 |
| P0 | ResourceManager + IRenderBackend | 纹理注册/删除流程跑通 |
| P0 | OpenGLBackend | `init` / `beginFrame` / `execute` / `present` 完整实现 |
| P0 | FontManager | 加载字体、生成字形图集、`createTextRun` |
| P0 | CommandBuffer | RectOp + TextRunOp 绘制 |
| P0 | WindowManager | GLFW 窗口创建、回调注册 |
| P0 | UIManager + Widget | InputField + ResultList |
| P0 | IndexDatabase | FTS5 搜索 + 排序 |
| P0 | AppScanner | 开始菜单扫描（.exe + .lnk） |
| P0 | LnkResolver | `IShellLinkW` 解析 |
| P0 | HotkeyManager | Alt+Space 呼出 |
| P0 | AppLauncher | `CreateProcess` + `ShellExecuteEx` |
| P1 | IconCache | 异步加载 + placeholder |
| P1 | AnimationEngine | 缩放淡入淡出 |
| P1 | TrayManager | 托盘图标 + 右键菜单 |
| P1 | ConfigManager | JSON 加载/保存 + .bak |
| P1 | StringUtils | 编码转换 |
| P1 | CrashHandler | 崩溃日志 |
| P1 | 单实例检查 | 命名互斥量 |

### Phase 2：完善与扩展

| 优先级 | 模块 | 交付物 |
|--------|------|--------|
| P1 | IMEWrapper | 中文预编辑 + 候选窗口 |
| P1 | PinyinConverter | 拼音首字母 + 多音字 |
| P1 | QueryCache | 查询缓存 + 增量失效 |
| P1 | FileWatcher | IOCP 文件监控 + 重新投递 |
| P2 | UWP 支持 | `IApplicationActivationManager` 启动 |
| P2 | UWP 扫描 | `PackageManager` 枚举 |
| P2 | ConfigPage | 内嵌设置页面 |
| P2 | 自定义命令 | Shell/SystemAPI/Script |
| P2 | 阴影/模糊特效 | ShadowOp + Blur 着色器 |
| P2 | WM_DPICHANGED | 多显示器 DPI 切换 |

### Phase 3：Vulkan 后端

| 优先级 | 模块 | 交付物 |
|--------|------|--------|
| P3 | VulkanBackend | `init` / `execute` / `present` |
| P3 | Fence 追踪 | 延迟释放 |
| P3 | 着色器 SPIR-V | 编译 GLSL → SPIR-V |

### Phase 4：扩展

- 插件系统（动态库）
- 网络搜索（Web API）
- 多语言界面

---

## 附录 A：审查闭环清单

### 第一轮（26 项）

| # | 类别 | 问题 | 修复章节 |
|---|------|------|---------|
| 1 | 矛盾 | 排序公式时间项符号错误 | §2.3 |
| 2 | 矛盾 | 延迟释放帧计数 vs 时间戳 | §5.3 |
| 3 | 矛盾 | ResourceManager data 悬空指针 | §5.2 |
| 4 | 矛盾 | IME 消息过滤漏 COMPOSITION | §5.14 |
| 5 | 矛盾 | 非模态对话框未集成消息泵 | §3.4 内嵌方案 |
| 6 | 不合理 | 首次扫描阻塞搜索 | §3.1 边扫边搜 |
| 7 | 不合理 | SearchCache 粗暴清空 | §5.20 增量失效 |
| 8 | 不合理 | 拼音放 FontManager | §5.19 独立模块 |
| 9 | 不合理 | ShellExecuteEx 自动提权误导 | §2.1 |
| 10 | 不合理 | 图标提取同步阻塞 | §5.6 异步 |
| 11 | 遗漏 | COM 初始化 | §6 main.cpp |
| 12 | 遗漏 | Vulkan Fence | §5.3 |
| 13 | 遗漏 | 配置面板通信 | §5.18 观察者 |
| 14 | 遗漏 | 字体图集溢出 | §5.5 多图集 |
| 15 | 遗漏 | 输入事件分发 | §5.10 |
| 16 | 遗漏 | 动画/布局接口 | §5.8 + §5.9 |
| 17 | 遗漏 | 日志轮转/配置恢复 | §5.16 + §5.18 |
| 18 | 遗漏 | 扫描并发/黑名单/符号链接 | §5.21 |
| 19 | 不清 | 离屏纹理引用 | §5.4 allocateTemporary |
| 20 | 不清 | ClipOp 实现 | §5.3 Scissor+Stencil |
| 21 | 不清 | IME 候选窗口位置 | §5.14 ImmSetCompositionWindow |
| 22 | 不清 | 脚本安全 | §2.2 白名单+默认禁用 |
| 23 | 不清 | FTS5 列权重 | §5.17 bm25(10,1,1,5,5) |
| 24 | 不清 | DPI 对话框 | §3.4 内嵌 GLFW |
| 25 | 不清 | Logger 路径写法 | §5.16 expandEnv |
| 26 | 不清 | FileWatcher 异步实现 | §5.15 IOCP |

### 第二轮（26 项）

| # | 类别 | 问题 | 修复章节 |
|---|------|------|---------|
| 1 | 矛盾 | CMake /MT vs 动态链接 | §7.3 /MD |
| 2 | 矛盾 | expandEnv private | §5.16 public |
| 3 | 矛盾 | SQL 截断高频项 | §2.3 取100条重排 |
| 4 | 矛盾 | OpenGL 三帧延迟冗余 | §5.3 直接删除 |
| 5 | 矛盾 | m_uiDirty 未定义 | §6 Application |
| 6 | 不合理 | 图标无重绘 | §5.6 invalidateCb |
| 7 | 不合理 | SearchCache 名不副实 | §5.20 QueryCache |
| 8 | 不合理 | 图集驱逐代价高 | §5.5 不驱逐 |
| 9 | 不合理 | 多音字未处理 | §5.19 多音字 |
| 10 | 不合理 | FileWatcher 重新投递缺失 | §5.15 armWatch |
| 11 | 遗漏 | 配置面板实现 | §5.24 ConfigPage |
| 12 | 遗漏 | Lnk 解析器 | §5.22 LnkResolver |
| 13 | 遗漏 | UWP 扫描 | §5.21 scanUWPApps |
| 14 | 遗漏 | fmt 依赖 | §7.2 vcpkg.json |
| 15 | 遗漏 | TrayManager HWND | §5.13 独立窗口 |
| 16 | 遗漏 | IME IMM 细节 | §5.14 完整代码 |
| 17 | 遗漏 | extractAppInfo | §5.21 |
| 18 | 遗漏 | 阴影着色器 | §5.25 GLSL |
| 19 | 遗漏 | 崩溃恢复 | §5.26 + §6 |
| 20 | 不清 | ShadowOp 输出 | §5.4 当前 FBO |
| 21 | 不清 | IME/GLFW 协调 | §5.10 m_imeActive |
| 22 | 不清 | ClipOp 继承 | §5.4 monostate 不继承 |
| 23 | 不清 | IconCache 并发去重 | §5.6 callback_list |
| 24 | 不清 | 消息优先级 | §5.12 |
| 25 | 不清 | Vulkan Fence 接口 | §5.3 notifyResourceUsed |
| 26 | 不清 | ConfigChangeEvent Alias | §5.18 AliasEntry |

### 第三轮自审（30 项）

| # | 类别 | 问题 | v5 修复 |
|---|------|------|---------|
| 1 | 矛盾 | §3.4 配置面板描述自相矛盾 | §3.4 整体重写为内嵌方案，删除 Win32 对话框残留描述 |
| 2 | 矛盾 | Application 缺 m_frameIndex | §6 Application 类添加 `uint64_t m_frameIndex = 0` |
| 3 | 矛盾 | IconCache 跨线程调 GL | §5.6 异步线程只提取像素，主线程 processPending() 注册纹理 |
| 4 | 矛盾 | PinyinConverter 单例+unique_ptr | §5.19 去掉 instance()，纯实例类 |
| 5 | 矛盾 | LnkResolver 重复 CoInitializeEx | §5.22 LnkResolver 不负责 COM，main.cpp 统一初始化 |
| 6 | 矛盾 | vcpkg.json 重复 sqlite3 | §7.2 合并为一条带 fts5 feature |
| 7 | 矛盾 | FileWatcher IOCP 未初始化 | §5.15 补充 CreateIoCompletionPort + armWatch 完整实现 |
| 8 | 矛盾 | FileWatcher 只检查第一个 watch | §5.15 统一 IOCP 端口，所有 watch 共享 |
| 9 | 不合理 | 搜索防抖未实现 | §6 添加 m_lastInputTime + m_searchPending + 主循环检查 |
| 10 | 不合理 | m_uiDirty 未检查 | §6 主循环检查 m_uiDirty \|\| isDirty \|\| hasActive |
| 11 | 不合理 | 窗口隐藏仍渲染 | §6 隐藏时 Sleep(50) 低功耗模式 |
| 12 | 不合理 | QueryCache::invalidateApp 复杂度高 | §5.20 少量增量/大量全量清空策略 |
| 13 | 不合理 | fontSizeBucket 未定义 | §5.5 明确 2px 粒度分桶 |
| 14 | 遗漏 | Widget 基类缺失 | §5.7 补充完整 Widget.hpp |
| 15 | 遗漏 | Application 方法声明缺失 | §6 Application.hpp 补充所有方法声明 |
| 16 | 遗漏 | OpenGLBackend 实现缺失 | Phase 1 P0 交付物（本蓝图给出接口+策略，实现代码在编码阶段产出） |
| 17 | 遗漏 | GLFW 回调注册 | §5.11 registerCallbacks() + §6 init() 中调用 |
| 18 | 遗漏 | WM_DPICHANGED 处理 | §5.11 windowProc 处理 + onDpiChanged 回调 |
| 19 | 遗漏 | 单实例检查 | §6 main.cpp CreateMutex |
| 20 | 遗漏 | 自定义命令系统 | §2.2 数据结构 + config.json 格式 + AppLauncher::launchCustomCommand |
| 21 | 遗漏 | wideToUtf8/utf8ToWide | §5.27 StringUtils |
| 22 | 遗漏 | 优雅关闭序列 | §6 shutdown() 完整实现 |
| 23 | 不清 | PinyinConverter 匹配算法 | §5.19 精确定义逐组匹配规则 + 示例 |
| 24 | 不清 | GlyphVertex UV 语义 | §5.4 改为标准 1pos+1uv，每字形 4 顶点 6 索引 |
| 25 | 不清 | RenderTargetOp Blit | §5.4 定义 Set/Reset/Blit 三个 action |
| 26 | 不清 | ConfigPage 鼠标路由 | §5.7 hit-test 遍历 |
| 27 | 不清 | unofficial-sqlite3 包名 | §7.3 注明需验证 vcpkg 版本 |
| 28 | 不清 | CrashHandler 依赖 Logger | §5.26 自包含，直接 GetEnvironmentVariable |
| 29 | 不清 | execute() 与特殊 Op 交互 | §5.3 线性遍历，RenderTargetOp/ApplyEffectOp 拆分批次 |
| 30 | 不清 | mini-pinyin 来源 | §5.19 + §7 明确 vendor 在 third_party/ |

---

## 附录 B：架构决策记录

| # | 决策 | 选择 | 理由 |
|---|------|------|------|
| 1 | 首个后端 | OpenGL | 生态成熟、调试方便、快速跑通全链路 |
| 2 | 第一版 UI 范围 | 输入框 + 结果列表 | 最小可用，控件树框架预留 |
| 3 | 包管理 | vcpkg | Windows 生态主流，CMake 集成好 |
| 4 | 延迟释放 | OpenGL 直接删除 / Vulkan Fence | 各后端用各自最合适的方式 |
| 5 | 像素数据所有权 | ResourceManager 深拷贝 | 避免 resync 时悬空崩溃 |
| 6 | 缓存失效 | 增量 + TTL + 大量全量 | 避免全量清空导致性能抖动 |
| 7 | 拼音转换 | 独立 PinyinConverter（纯实例） | 职责单一，不耦合字体 |
| 8 | 图标加载 | 异步提取像素 + 主线程注册 + invalidate | 避免 SHGetFileInfo 阻塞 UI + 避免跨线程 GL |
| 9 | FileWatcher | IOCP + 重新投递 + 统一端口 | 异步非阻塞，线程可优雅退出 |
| 10 | 配置面板 | 内嵌 GLFW 页面 | 避免 Win32 对话框 DPI 问题，复用渲染管线 |
| 11 | 链接策略 | 动态链接 /MD | 与 ≤8MB 体积目标一致 |
| 12 | SQL + C++ 排序 | SQL 取 100 条，C++ 重排取 20 | 兼顾 bm25 匹配与高频优先 |
| 13 | 字体图集 | 多图集不驱逐 + 2px 分桶 | 避免 LRU 驱逐导致大规模重新光栅化 |
| 14 | TrayManager | 独立隐藏窗口 | 避免与 GLFW 窗口过程冲突 |
| 15 | 图标并发去重 | path → callback_list | 避免重复加载同一图标 |
| 16 | 配置同步 | 观察者模式 + ConfigChangeEvent | 解耦配置面板与主窗口 |
| 17 | 多音字 | 存储所有读音首字母 | 覆盖用户可能的输入习惯 |
| 18 | 崩溃处理 | CrashHandler（自包含） + try/catch | 写崩溃日志，优雅退出 |
| 19 | COM 初始化 | main.cpp 统一调用 | LnkResolver/AppLauncher 不重复 |
| 20 | 单实例 | CreateMutex | 防止多实例 |
| 21 | 搜索防抖 | steady_clock + 主循环检查 | 50ms 防抖 |
| 22 | 窗口隐藏低功耗 | Sleep(50) | 降低空闲 CPU |
| 23 | ClipOp 继承 | monostate = 无裁剪（不继承） | 语义明确 |
| 24 | IME/GLFW 协调 | m_imeActive 时忽略 onChar | 避免重复输入 |
| 25 | PinyinConverter 匹配 | 逐组匹配，每组最多消耗 1 字符 | 精确支持多音字 |

---

*文档结束。*
*v5 蓝图已合并原始策划书 + 第一轮 26 项 + 第二轮 26 项 + 第三轮自审 30 项 = 共 82 项审查闭环。*
*所有模块均有完整 C++ 头文件接口定义和关键实现代码，可直接开始编码。*
