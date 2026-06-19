@echo off
chcp 65001 >nul
echo ===================================
echo   TimeoutKill - Clean Build
echo ===================================
echo.

if exist "build" (
    echo [OK] Removing build directory...
    rmdir /s /q build
    echo [OK] Done.
) else (
    echo [SKIP] build directory not found.
)

echo.
pause
