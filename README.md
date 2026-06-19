# TimeoutKill - 进程超时自动终止工具

## 功能

监控指定进程，连续运行超过设定时间后自动结束。

- 每隔固定时间检查一次目标进程是否存活
- 目标存活 → 计数 +1
- 目标消失 → 计数归零
- 累计达到最大次数 → 强制结束目标进程，并继续监控下一轮
- 进程名不区分大小写

## 两种使用方式

### GUI 模式

直接双击运行或右键以管理员身份运行，通过界面操作。

### 命令行模式

```
TimeoutKill.exe --help                        显示帮助
TimeoutKill.exe start --process <名称> [选项]  直接启动监控
```

**命令行选项：**

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `--process <名称>` | 目标进程名（不含路径） | 必填 |
| `--count <次数>` | 最大监视次数 | 20 |
| `--interval <分钟>` | 每次检查间隔（分钟） | 30 |

**示例：**

```cmd
TimeoutKill.exe start --process chrome.exe --count 10 --interval 5
```

每 5 分钟检查一次 chrome.exe，累计 10 次后终止。命令行模式下按 **Ctrl+C** 随时停止。

## 安全特性

- **互斥锁**：确保程序唯一运行，重复启动时弹窗提示
- **管理员权限**：通过 UAC manifest 自动请求提权
- **运行日志**：记录到 `TimeoutKill.log` 文件和界面（每次启动清空旧日志）

## 构建

### 前置要求

- Visual Studio 2022（或更新版本）+ C++ 桌面开发组件
- CMake 3.20+

### 快速构建

1. 打开 "Developer Command Prompt for VS 2022"
2. 运行 `build.bat`

### 清理

运行 `clean.bat` 一键删除 `build` 目录。

### 输出

```
bin/TimeoutKill.exe              ← 自动复制的 Release 成品
build/bin/Release/TimeoutKill.exe
build/bin/Debug/TimeoutKill.exe  ← 调试版（有控制台）
```

## 技术栈

- C++20 / MSVC
- Win32 API（原生 GUI + 命令行双模式）
- Per-Monitor V2 高 DPI 支持
- CMake 构建

## 文件说明

| 文件 | 说明 |
|------|------|
| `main.cpp` | 主程序源码 |
| `CMakeLists.txt` | CMake 构建配置 |
| `TimeoutKill.manifest` | UAC 管理员权限 + DPI 声明 |
| `build.bat` | 一键构建脚本 |
| `clean.bat` | 一键清理构建产物 |
| `TimeoutKill.log` | 运行日志（每次启动重建） |
| `TimeoutKill.cfg` | 配置文件（自动生成，保存上次参数） |
