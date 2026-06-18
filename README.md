# TimeoutKill - 进程超时自动终止工具

## 功能

监控指定进程，连续运行超过设定时间后自动结束。

- 每隔固定时间检查一次目标进程是否存活
- 目标存活 → 计数 +1
- 目标消失 → 计数归零
- 累计达到最大次数 → 强制结束目标进程，并继续监控下一轮
- 进程名不区分大小写

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

### 手动构建

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cmake --build build --config Debug
```

### 清理

运行 `clean.bat` 一键删除 `build` 目录。

### 输出位置

```
build/bin/Release/TimeoutKill.exe   ← 发布版（无控制台）
build/bin/Debug/TimeoutKill.exe     ← 调试版（有控制台）
```

## 使用

1. 右键 `TimeoutKill.exe` → **以管理员身份运行**
2. 输入目标进程名（不含路径，如 `chrome.exe`）
3. 设置最大监视次数和每次等待时间
4. 点击「开始监控」
5. 可随时修改参数、停止监控或退出

## 技术栈

- C++20 / MSVC
- Win32 API（原生 GUI，无第三方依赖）
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
