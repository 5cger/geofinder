# GeoFinder

**Windows 应用/文件快速启动器** — GLFW + OpenGL 渲染，哈希表索引，拼音搜索，自定义窗口界面。

## 功能

- **全局热键呼出** — Alt+Space 在任何界面弹出搜索窗口
- **实时模糊搜索** — 输入即搜（50ms 防抖），支持拼音首字母
- **哈希表索引** — 内存三级索引（名称/拼音/路径），二进制缓存持久化，启动即用
- **自定义窗口** — 无边框自绘标题栏，拖拽移动，任务栏无图标
- **系统托盘** — 右键菜单显示/退出
- **@扩展名过滤** — 输入 `@pdf:` 搜索 PDF 文件
- **目录浏览** — 输入 `C:\` 直接浏览目录内容
- **`.exe` / `.lnk` 优先** — 可执行文件在搜索结果中置顶
- **中文拼音支持** — `wyyyy` 匹配「网易云音乐」
- **文件监控** — 新文件自动加入索引
- **设置页 GUI** — 可视化扫描目录管理 + 动画速度调节

## 构建

### 依赖

| 库 | 安装（MSYS2） |
|----|--------------|
| GLFW | `pacman -S mingw-w64-x86_64-glfw` |
| GLEW | `pacman -S mingw-w64-x86_64-glew` |
| GLM | `pacman -S mingw-w64-x86_64-glm` |
| FreeType | `pacman -S mingw-w64-x86_64-freetype` |
| SQLite3 | `pacman -S mingw-w64-x86_64-sqlite3` |
| nlohmann-json | `pacman -S mingw-w64-x86_64-nlohmann-json` |

### 编译

```bash
build.bat
# 或手动:
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
```

### 运行

```bash
build/bin/geofinder.exe
```

## 快捷键

| 按键 | 功能 |
|------|------|
| Alt+Space | 呼出/隐藏窗口 |
| ↑ ↓ | 选择结果 |
| Enter | 启动选中项 |
| Esc | 清空输入 / 隐藏窗口 |
| Ctrl+O | 选中文件所在文件夹 |
| Ctrl+R | 重新扫描（设置页） |

## 许可证

MIT
