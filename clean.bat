@echo off
chcp 65001 >nul
echo ===================================
echo   TimeoutKill - 清理构建产物
echo ===================================
echo.

if exist "build" (
    echo [清理] 删除 build 目录...
    rmdir /s /q build
    echo [完成] build 目录已删除
) else (
    echo [跳过] build 目录不存在
)

echo.
echo 清理完毕！
pause
