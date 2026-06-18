@echo off
chcp 65001 >nul
echo ===================================
echo   TimeoutKill - 构建脚本
echo ===================================
echo.

where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] 未找到 cmake
    pause
    exit /b 1
)

where cl >nul 2>&1
if %errorlevel% neq 0 (
    echo [提示] 请先在 VS 开发者命令行中运行此脚本
    pause
    exit /b 1
)

echo [1/2] 配置项目...
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if %errorlevel% neq 0 ( pause & exit /b 1 )

echo.
echo [2/2] 编译 Release + Debug...
cmake --build build --config Release
cmake --build build --config Debug

echo.
echo ===================================
echo   Release: build\bin\Release\TimeoutKill.exe (无控制台)
echo   Debug:   build\bin\Debug\TimeoutKill.exe   (有控制台)
echo ===================================
echo.
pause
