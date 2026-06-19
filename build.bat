@echo off
setlocal enabledelayedexpansion
echo ===================================
echo   TimeoutKill - Build Script
echo ===================================
echo.

:: 检查并加载 VS 开发者环境
where cl >nul 2>&1
if !errorlevel! neq 0 (
    echo [INFO] Setting up VS environment...
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" x64
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64
    ) else (
        echo [ERROR] Visual Studio 2022 not found
        pause
        exit /b 1
    )
    :: vcvarsall.bat 覆盖了 PATH，把 System32 加回来
    set "PATH=%SystemRoot%\System32;!PATH!"
)

where cl >nul 2>&1
if !errorlevel! neq 0 (
    echo [ERROR] MSVC compiler not available
    pause
    exit /b 1
)

where cmake >nul 2>&1
if !errorlevel! neq 0 (
    echo [ERROR] cmake not found
    pause
    exit /b 1
)

echo [1/2] Configuring...
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if !errorlevel! neq 0 ( pause & exit /b 1 )

echo.
echo [2/2] Building Release + Debug...
cmake --build build --config Release
cmake --build build --config Debug

echo.
echo ===================================
echo   Release: build\bin\Release\TimeoutKill.exe
echo   Debug:   build\bin\Debug\TimeoutKill.exe
echo ===================================
echo.
pause
