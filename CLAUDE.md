# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**GeoFinder** — a GLFW + OpenGL (with optional Vulkan backend) Windows 10/11 application launcher, built in C++17. Think of it as a lightweight, GPU-rendered alternative to tools like Wox/Flow Launcher/uTools.

## Authoritative Spec

[GeoFinder_完整技术蓝图_v5.md](GeoFinder_完整技术蓝图_v5.md) is the **single source of truth** for all architecture decisions, interface definitions, data structures, and algorithms. Any conflict between the blueprint and other sources → the blueprint wins. If the blueprint is ambiguous or contradictory, pause and ask for clarification before implementing.

## Build System

- **Compiler:** MinGW (MSYS2, GCC 16), flags: `-Wall -Wextra -Wpedantic -Wshadow`
- **Build tool:** CMake 3.20+
- **Package manager:** MSYS2 pacman (shared with vibebar/c2 project)
- **Dependencies (MSYS2):** `glfw3`, `freetype`, `sqlite3`, `nlohmann-json`, `fmt`
- **Dependencies (vendored):** `third_party/mini-pinyin/` (lightweight pinyin library)
- **No vcpkg, no Skia** — raw OpenGL via GLFW context

### Key Build Commands

```bash
# One-click build
build.bat

# Or manual configure + build
cmake -B build -S . -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4

# Build with tests
cmake -B build -S . -G "MinGW Makefiles" -DGEOFINDER_BUILD_TESTS=ON
cmake --build build -j4

# Run tests
cd build && ctest --output-on-failure
```

## Architecture

### Layer Stack (top-down, strict单向 dependencies)

| Layer | Responsibility | Key Modules |
|---|---|---|
| Application | Main loop, DI, lifecycle | `Application` |
| UI System | Widget tree, layout, events, animation | `UIManager`, `Widget`, `LayoutEngine`, `AnimationEngine` |
| Resource System | Font & icon loading, pixel data | `FontManager`, `IconCache`, `ResourceManager` |
| Command Buffer | Draw/effect command container | `CommandBuffer` |
| Render Backend | OpenGL/Vulkan implementation | `IRenderBackend` → `OpenGLBackend`, `VulkanBackend` |
| System Services | Window, hotkey, tray, IME, file watch, logging | `WindowManager`, `HotkeyManager`, `TrayManager`, `IMEWrapper`, `FileWatcher`, `Logger`, `CrashHandler` |
| Data Persistence | SQLite index, JSON config, pinyin, query cache | `IndexDatabase`, `ConfigManager`, `PinyinConverter`, `QueryCache` |
| Utilities | Cross-cutting tools | `StringUtils`, `LnkResolver`, `AppScanner`, `AppLauncher` |

### Dependency Rules
- `Application` owns all subsystems and injects dependencies downward.
- UI layer depends on `CommandBuffer` + `ResourceManager` (handles only), never on concrete render backends.
- Resource layer depends on `IRenderBackend` abstraction, never on concrete GPU APIs.
- System services and data layers are independent of each other; both expose to `Application` only.
- Utility layer has no dependency restrictions — any layer may use it.
- **All code in `namespace geofinder`.**

### Directory Structure

See blueprint §4.3 for the full tree. Top-level layout:
```
src/app/       — Application, AppLauncher, CrashHandler
src/ui/        — Widget base, InputField, ResultList, ConfigPage, UIManager, LayoutEngine, AnimationEngine
src/render/    — IRenderBackend, CommandBuffer, OpenGLBackend, VulkanBackend, shaders/
src/resource/  — ResourceManager, FontManager, IconCache
src/system/    — WindowManager, HotkeyManager, TrayManager, IMEWrapper, FileWatcher, Logger
src/data/      — IndexDatabase, ConfigManager, PinyinConverter, QueryCache, AppScanner, LnkResolver
src/utils/     — StringUtils
third_party/mini-pinyin/ — vendored pinyin library
tests/         — unit tests
```

## Development Process

### Sprint-Based (13 sprints, see prompt.md lines 46-114)

Each sprint follows a strict flow:
1. **Header-first:** Write all `.hpp` interfaces → get approval → then implement `.cpp`.
2. **Blueprint references:** Every class/function must be annotated with `// 参考蓝图 §X.Y`.
3. **TODO markers:** Use `// TODO(SprintN): description` for deferred work.
4. **Completion report:** After each sprint, report files changed, key decisions, compile result.

### Current Sprint: 0 (Project Skeleton) ✅

Outputs: `CMakeLists.txt`, `cmake/Dependencies.cmake`, `build.bat`, `src/main.cpp`, `src/app/Application.hpp`, `src/app/Application.cpp` (stub).

## Hard Constraints (Red Lines)

These are non-negotiable — violating any means rollback:

1. **Thread Safety:** OpenGL/Vulkan functions **only on the main thread**. Worker threads (icon extraction, file scanning) must never touch GPU APIs.
2. **COM Constraint:** `CoInitializeEx` called **once** in `wWinMain`. Modules using COM (`LnkResolver`, `AppLauncher`) must not re-initialize.
3. **Memory Ownership:** `ResourceManager` must own deep copies (`std::vector<uint8_t>`) of pixel data — never store external pointers.
4. **IME Priority:** When IME is active, Esc clears preedit text first; when IME is inactive, Esc clears input or hides window. Never mix these up.
5. **IOCP Completeness:** `FileWatcher` must re-issue `ReadDirectoryChangesW` after every notification, or monitoring silently dies.
6. **Interface Freeze:** Header file interfaces (function signatures, class names, enum values, file paths) from the blueprint are frozen. Implementation details can be discussed; interface changes require explicit approval.

## Coding Standards

- **Encoding:** UTF-8 source, UTF-16 for Win32 API interactions
- **Platform:** Windows 10 1903+ / Windows 11 x64, PerMonitorV2 DPI awareness
- **DPI:** Always use `dpiScale` factor; never hardcode pixel values
- **Every sprint must pass:** zero warnings (`/W4`), no memory leaks, thread safety, interface consistency with blueprint, error handling with fallback paths, no UI blocking >16ms

## Testing

- **Framework:** Catch2 (via vcpkg)
- Test files per module: `test_fontmanager.cpp`, `test_pinyin.cpp`, `test_querycache.cpp`, `test_resourcemanager.cpp`, `test_stringutils.cpp`, `test_scanner.cpp`
- Performance baseline: search <30ms for 5000 entries, memory <45MB, exe ≤8MB

## 1. 编码前先思考

**不要主观臆断。不要掩盖疑惑。把权衡取舍摆到台面上。**

在进行实现之前：

- 明确陈述你的假设。如果不确定，请提问。

- 如果存在多种解释，请将它们列出——不要默默地自行决定。

- 如果有更简单的方案，请提出来。在有正当理由时提出反对意见。

- 如果有任何不清楚的地方，请停下来。指明让人困惑的地方。提问。

## 2. 简单优先

**用最少的代码解决问题。不做任何预测性开发。**

- 不添加要求之外的功能。

- 不为一次性代码做抽象处理。

- 不提供未被要求的“灵活性”或“可配置性”。

- 不为不可能发生的场景编写错误处理。

- 如果你写了 200 行代码，但其实 50 行就能搞定，请重写。

问问你自己：“高级工程师会认为这太复杂了吗？”如果是，请简化。

## 3. 外科手术般的精准修改

**只改动必须改动的地方。只清理你自己制造的烂摊子。**

在编辑现有代码时：

- 不要去“改进”相邻的代码、注释或格式。

- 不要重构没有出问题的东西。

- 保持现有的代码风格，即使你个人倾向于不同的做法。

- 如果你注意到不相关的死代码，可以提出来——但不要删除它。

当你的修改产生了孤立（弃用）的代码时：

- 删除由于**你的**修改而变得不再使用的导入（imports）、变量或函数。

- 除非被明确要求，否则不要删除原先就存在的死代码。

检验标准：每一行被修改的代码都应该能直接追溯到用户的请求。

## 4. 目标驱动执行

**定义成功标准。循环直到验证通过。**

将任务转化为可验证的目标：

- “添加验证” → “为无效输入编写测试，然后让它们通过”

- “修复 bug” → “编写一个能复现该 bug 的测试，然后让它通过”

- “重构 X” → “确保重构前后测试都能通过”

对于多步任务，请简述计划：

```

1. [步骤] → 验证：[检查项]

2. [步骤] → 验证：[检查项]

3. [步骤] → 验证：[检查项]

```

明确的成功标准能让你独立进行循环工作。模糊的标准（“让它跑起来”）则需要不断地确认。
