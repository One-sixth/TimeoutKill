#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <algorithm>
#include <cstdlib>

// 全局常量
constexpr const wchar_t* MUTEX_NAME = L"Global\\TimeoutKill_v1";
constexpr const wchar_t* WINDOW_CLASS = L"TimeoutKillClass";
constexpr const wchar_t* WINDOW_TITLE = L"TimeoutKill";
constexpr int BASE_DPI = 96;  // 设计基准 DPI

// 控件 ID
constexpr int ID_EDIT_PROCESS  = 1001;
constexpr int ID_EDIT_COUNT    = 1002;
constexpr int ID_EDIT_INTERVAL = 1003;
constexpr int ID_BTN_TOGGLE    = 2001;
constexpr int ID_BTN_EXIT      = 2002;

// 全局变量
static HINSTANCE g_hInstance = nullptr;
static HWND g_hWnd = nullptr;
static HWND g_hBtnToggle = nullptr;
static HWND g_hBtnExit = nullptr;
static HWND g_hStatus = nullptr;
static HWND g_hEditProcess = nullptr;
static HWND g_hEditCount = nullptr;
static HWND g_hEditInterval = nullptr;
static HWND g_hLabelCount = nullptr;
static HWND g_hLog = nullptr;

static HANDLE g_hMutex = nullptr;
static HANDLE g_hThread = nullptr;
static bool g_running = false;
static bool g_threadActive = false;
static int g_checkCount = 0;
static int g_maxChecks = 20;
static int g_intervalMin = 30;
static std::wstring g_processName;
static HFONT g_hFont = nullptr;
static HBRUSH g_hStatusBrush = nullptr;
static int g_dpi = 96;  // 当前 DPI

// ========== DPI 缩放 ==========

static int Scale(int value) {
    return ::MulDiv(value, g_dpi, BASE_DPI);
}

static int GetSystemDpi() {
    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);
    return dpi;
}

// ========== 工具函数 ==========

static std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    auto pos = path.find_last_of(L'\\');
    return (pos != std::wstring::npos) ? path.substr(0, pos) : path;
}

static std::wstring GetLogFile() {
    return GetExeDir() + L"\\TimeoutKill.log";
}

static int GetEditTextInt(HWND hEdit, int defaultVal) {
    wchar_t buf[32] = {};
    GetWindowTextW(hEdit, buf, 32);
    if (lstrlenW(buf) == 0) return defaultVal;
    int val = _wtoi(buf);
    return (val > 0) ? val : defaultVal;
}

static void StartNewLog() {
    HANDLE hFile = CreateFileW(GetLogFile().c_str(), GENERIC_WRITE,
        0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        wchar_t bom = 0xFEFF;
        WriteFile(hFile, &bom, sizeof(bom), &written, nullptr);
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t line[128];
        wsprintfW(line, L"[%04d-%02d-%02d %02d:%02d:%02d] ========== 程序启动 ==========\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        WriteFile(hFile, line, lstrlenW(line) * sizeof(wchar_t), &written, nullptr);
        CloseHandle(hFile);
    }
    if (g_hLog) SetWindowTextW(g_hLog, L"");
}

static void AppendLog(const std::wstring& text) {
    HANDLE hFile = CreateFileW(GetLogFile().c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        SetFilePointer(hFile, 0, nullptr, FILE_END);
        DWORD written;
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t line[512];
        wsprintfW(line, L"[%04d-%02d-%02d %02d:%02d:%02d] %s\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, text.c_str());
        WriteFile(hFile, line, lstrlenW(line) * sizeof(wchar_t), &written, nullptr);
        CloseHandle(hFile);
    }
    if (g_hLog) {
        int len = GetWindowTextLengthW(g_hLog);
        SendMessageW(g_hLog, EM_SETSEL, len, len);
        std::wstring line = text + L"\r\n";
        SendMessageW(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
        SendMessageW(g_hLog, EM_SCROLLCARET, 0, 0);
    }
}

static void SetStatus(const wchar_t* text, COLORREF color) {
    if (g_hStatus) {
        SetWindowTextW(g_hStatus, text);
        if (g_hStatusBrush) DeleteObject(g_hStatusBrush);
        g_hStatusBrush = CreateSolidBrush(color);
        InvalidateRect(g_hStatus, nullptr, TRUE);
    }
}

static void UpdateCountDisplay() {
    if (g_hLabelCount) {
        wchar_t buf[128];
        wsprintfW(buf, L"%d / %d", g_checkCount, g_maxChecks);
        SetWindowTextW(g_hLabelCount, buf);
    }
}

// ========== 进程检测 ==========

static bool IsProcessRunning(const std::wstring& processName) {
    if (processName.empty()) return false;

    std::wstring target = processName;
    std::transform(target.begin(), target.end(), target.begin(), ::towlower);

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    bool found = false;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            std::wstring name(pe.szExeFile);
            std::transform(name.begin(), name.end(), name.begin(), ::towlower);
            if (name == target) {
                found = true;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return found;
}

// ========== 定时器线程 ==========

static DWORD WINAPI TimerThread(LPVOID) {
    AppendLog(L"[启动] 监控线程开始运行");

    while (g_threadActive) {
        int waitSeconds = g_intervalMin * 60;
        for (int i = 0; i < waitSeconds && g_threadActive; ++i) {
            Sleep(1000);
        }
        if (!g_threadActive) break;

        wchar_t procBuf[256] = {};
        if (g_hEditProcess) {
            SendMessageW(g_hEditProcess, WM_GETTEXT, 256, (LPARAM)procBuf);
        }
        g_processName = procBuf;
        g_maxChecks = GetEditTextInt(g_hEditCount, 20);

        if (g_processName.empty()) {
            AppendLog(L"[跳过] 进程名为空，等待设置");
            continue;
        }

        if (IsProcessRunning(g_processName)) {
            g_checkCount++;
            wchar_t log[256];
            wsprintfW(log, L"[存活] %s 仍然运行中 (计数: %d/%d)",
                g_processName.c_str(), g_checkCount, g_maxChecks);
            AppendLog(log);
            UpdateCountDisplay();

            if (g_checkCount >= g_maxChecks) {
                AppendLog(L"[超时] 达到最大计数，执行强制结束！");
                PROCESSENTRY32W pe = {};
                pe.dwSize = sizeof(pe);
                HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                if (hSnap != INVALID_HANDLE_VALUE) {
                    std::wstring target = g_processName;
                    std::transform(target.begin(), target.end(), target.begin(), ::towlower);
                    if (Process32FirstW(hSnap, &pe)) {
                        do {
                            std::wstring name(pe.szExeFile);
                            std::transform(name.begin(), name.end(), name.begin(), ::towlower);
                            if (name == target) {
                                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                                if (hProc) {
                                    TerminateProcess(hProc, 0);
                                    CloseHandle(hProc);
                                    wchar_t log2[256];
                                    wsprintfW(log2, L"[结束] 已终止进程 PID=%lu", pe.th32ProcessID);
                                    AppendLog(log2);
                                }
                            }
                        } while (Process32NextW(hSnap, &pe));
                        CloseHandle(hSnap);
                    }
                }
                g_checkCount = 0;
                UpdateCountDisplay();
                SetStatus(L"已终止目标，继续监控中", RGB(50, 180, 50));
                AppendLog(L"[重置] 计数归零，继续监控下一轮");
            }
        } else {
            if (g_checkCount > 0) {
                wchar_t log[256];
                wsprintfW(log, L"[消失] %s 已不在运行，计数重置为0", g_processName.c_str());
                AppendLog(log);
            }
            g_checkCount = 0;
            UpdateCountDisplay();
            SetStatus(L"监控中 - 目标未检测到", RGB(100, 180, 100));
        }
    }

    AppendLog(L"[停止] 监控线程已退出");
    return 0;
}

static void StartMonitoring() {
    if (g_threadActive) return;

    g_running = true;
    g_checkCount = 0;
    g_maxChecks = GetEditTextInt(g_hEditCount, 20);
    g_intervalMin = GetEditTextInt(g_hEditInterval, 30);

    wchar_t cfgPath[MAX_PATH];
    wsprintfW(cfgPath, L"%s\\TimeoutKill.cfg", GetExeDir().c_str());
    wchar_t procBuf[256] = {};
    GetWindowTextW(g_hEditProcess, procBuf, 256);
    WritePrivateProfileStringW(L"Main", L"ProcessName", procBuf, cfgPath);
    wchar_t tmp[32];
    wsprintfW(tmp, L"%d", g_maxChecks);
    WritePrivateProfileStringW(L"Main", L"MaxChecks", tmp, cfgPath);
    wsprintfW(tmp, L"%d", g_intervalMin);
    WritePrivateProfileStringW(L"Main", L"IntervalMin", tmp, cfgPath);

    wchar_t log[256];
    wsprintfW(log, L"配置: 进程=%s, 最大次数=%d, 间隔=%d分钟, DPI=%d",
        procBuf, g_maxChecks, g_intervalMin, g_dpi);
    AppendLog(log);

    g_threadActive = true;
    g_hThread = CreateThread(nullptr, 0, TimerThread, nullptr, 0, nullptr);
    SetStatus(L"监控中", RGB(50, 180, 50));
    UpdateCountDisplay();
    SetWindowTextW(g_hBtnToggle, L"停止监控");
}

static void StopMonitoring() {
    if (!g_threadActive) return;

    g_threadActive = false;
    if (g_hThread) {
        WaitForSingleObject(g_hThread, 5000);
        CloseHandle(g_hThread);
        g_hThread = nullptr;
    }
    g_running = false;
    SetStatus(L"已停止", RGB(150, 150, 150));
    SetWindowTextW(g_hBtnToggle, L"开始监控");
}

// ========== 窗口过程 ==========

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // 根据 DPI 创建字体：基准 14pt
        int fontHeight = -Scale(14);
        g_hFont = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft YaHei UI");

        int x = Scale(20), y = Scale(12), w = Scale(500);

        // ---- 进程名 ----
        HWND hTmp = CreateWindowW(L"static", L"目标进程名（不含路径）：",
            WS_CHILD | WS_VISIBLE, x, y + Scale(3), w, Scale(22), hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTmp, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hEditProcess = CreateWindowExW(WS_EX_CLIENTEDGE, L"edit", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            x, y + Scale(25), w, Scale(26), hWnd, (HMENU)(INT_PTR)ID_EDIT_PROCESS, g_hInstance, nullptr);
        SendMessageW(g_hEditProcess, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hEditProcess, EM_SETLIMITTEXT, 255, 0);

        y += Scale(62);

        // ---- 最大监视次数 + 每次等待时间 ----
        hTmp = CreateWindowW(L"static", L"最大监视次数：",
            WS_CHILD | WS_VISIBLE, x, y + Scale(3), Scale(110), Scale(22), hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTmp, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hEditCount = CreateWindowExW(WS_EX_CLIENTEDGE, L"edit", L"20",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
            x + Scale(112), y, Scale(60), Scale(26), hWnd, (HMENU)(INT_PTR)ID_EDIT_COUNT, g_hInstance, nullptr);
        SendMessageW(g_hEditCount, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        hTmp = CreateWindowW(L"static", L"次",
            WS_CHILD | WS_VISIBLE, x + Scale(178), y + Scale(3), Scale(20), Scale(22), hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTmp, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        hTmp = CreateWindowW(L"static", L"每次等待：",
            WS_CHILD | WS_VISIBLE, x + Scale(230), y + Scale(3), Scale(85), Scale(22), hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTmp, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hEditInterval = CreateWindowExW(WS_EX_CLIENTEDGE, L"edit", L"30",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
            x + Scale(317), y, Scale(60), Scale(26), hWnd, (HMENU)(INT_PTR)ID_EDIT_INTERVAL, g_hInstance, nullptr);
        SendMessageW(g_hEditInterval, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        hTmp = CreateWindowW(L"static", L"分钟",
            WS_CHILD | WS_VISIBLE, x + Scale(383), y + Scale(3), Scale(30), Scale(22), hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTmp, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        y += Scale(36);

        // ---- 实时计数显示 ----
        g_hLabelCount = CreateWindowW(L"static", L"0 / 20",
            WS_CHILD | WS_VISIBLE | SS_CENTER, x + Scale(340), y, Scale(160), Scale(22),
            hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(g_hLabelCount, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        hTmp = CreateWindowW(L"static", L"当前计数：",
            WS_CHILD | WS_VISIBLE, x, y + Scale(2), Scale(85), Scale(22),
            hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTmp, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        y += Scale(30);

        // ---- 说明文字 ----
        HWND hInfo = CreateWindowW(L"static",
            L"目标存活则计数+1，消失则归零。\n累计达到最大次数后自动结束目标进程。",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            x, y, w, Scale(36), hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hInfo, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        y += Scale(44);

        // ---- 按钮 ----
        g_hBtnToggle = CreateWindowW(L"button", L"开始监控",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, y, Scale(130), Scale(36), hWnd, (HMENU)(INT_PTR)ID_BTN_TOGGLE, g_hInstance, nullptr);
        SendMessageW(g_hBtnToggle, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hBtnExit = CreateWindowW(L"button", L"退出程序",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x + Scale(150), y, Scale(130), Scale(36), hWnd, (HMENU)(INT_PTR)ID_BTN_EXIT, g_hInstance, nullptr);
        SendMessageW(g_hBtnExit, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        y += Scale(46);

        // ---- 状态栏 ----
        g_hStatus = CreateWindowW(L"static", L"就绪 - 请输入进程名",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            x, y, w, Scale(28), hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        y += Scale(36);

        // ---- 日志区域 ----
        hTmp = CreateWindowW(L"static", L"运行日志：",
            WS_CHILD | WS_VISIBLE, x, y, Scale(100), Scale(20),
            hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTmp, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        y += Scale(20);

        g_hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"edit", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            x, y, w, Scale(160), hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(g_hLog, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hLog, EM_SETLIMITTEXT, 1024 * 1024, 0);

        // ---- 读取上次保存的设置 ----
        wchar_t cfgPath[MAX_PATH];
        wsprintfW(cfgPath, L"%s\\TimeoutKill.cfg", GetExeDir().c_str());
        wchar_t saved[256] = {};

        GetPrivateProfileStringW(L"Main", L"ProcessName", L"", saved, 256, cfgPath);
        if (lstrlenW(saved) > 0) SetWindowTextW(g_hEditProcess, saved);

        GetPrivateProfileStringW(L"Main", L"MaxChecks", L"20", saved, 256, cfgPath);
        SetWindowTextW(g_hEditCount, saved);

        GetPrivateProfileStringW(L"Main", L"IntervalMin", L"30", saved, 256, cfgPath);
        SetWindowTextW(g_hEditInterval, saved);

        g_maxChecks = GetEditTextInt(g_hEditCount, 20);
        g_intervalMin = GetEditTextInt(g_hEditInterval, 30);
        UpdateCountDisplay();

        StartNewLog();
        break;
    }

    // DPI 变化时重建字体和重新布局
    case WM_DPICHANGED: {
        g_dpi = HIWORD(wParam);
        // 重建字体
        if (g_hFont) DeleteObject(g_hFont);
        int fontHeight = -Scale(14);
        g_hFont = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft YaHei UI");
        // 广播新字体给所有子控件
        EnumChildWindows(hWnd, [](HWND hChild, LPARAM lParam) -> BOOL {
            SendMessageW(hChild, WM_SETFONT, (WPARAM)lParam, TRUE);
            return TRUE;
        }, (LPARAM)g_hFont);
        // 按推荐大小调整窗口
        if (lParam) {
            RECT* rc = (RECT*)lParam;
            SetWindowPos(hWnd, nullptr, rc->left, rc->top,
                rc->right - rc->left, rc->bottom - rc->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_BTN_TOGGLE:
            if (g_threadActive) {
                StopMonitoring();
            } else {
                wchar_t procBuf[256] = {};
                GetWindowTextW(g_hEditProcess, procBuf, 256);
                g_processName = procBuf;
                if (g_processName.empty()) {
                    MessageBoxW(hWnd, L"请先输入目标进程名！", L"提示", MB_ICONINFORMATION);
                    break;
                }
                g_maxChecks = GetEditTextInt(g_hEditCount, 20);
                g_intervalMin = GetEditTextInt(g_hEditInterval, 30);
                if (g_maxChecks <= 0 || g_intervalMin <= 0) {
                    MessageBoxW(hWnd, L"监视次数和等待时间必须大于0！", L"提示", MB_ICONWARNING);
                    break;
                }
                wchar_t log[256];
                wsprintfW(log, L"开始监控进程: %s", g_processName.c_str());
                AppendLog(log);
                StartMonitoring();
            }
            break;

        case ID_BTN_EXIT:
            StopMonitoring();
            DestroyWindow(hWnd);
            break;

        case ID_EDIT_COUNT:
        case ID_EDIT_INTERVAL:
            if (HIWORD(wParam) == EN_CHANGE) {
                g_maxChecks = GetEditTextInt(g_hEditCount, 20);
                g_intervalMin = GetEditTextInt(g_hEditInterval, 30);
                UpdateCountDisplay();
            }
            break;
        }
        break;

    case WM_USER + 1:
        StopMonitoring();
        break;

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        if (hCtrl == g_hStatus && g_hStatusBrush) {
            SetBkColor(hdc, RGB(240, 240, 240));
            return (LRESULT)g_hStatusBrush;
        }
        break;
    }

    case WM_CLOSE:
        StopMonitoring();
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hStatusBrush) DeleteObject(g_hStatusBrush);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// ========== 入口点 ==========

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nShow) {
    g_hInstance = hInstance;

    // 设置 DPI 感知（Per-Monitor V2，窗口内容随显示器 DPI 自动缩放）
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_dpi = GetSystemDpi();

    g_hMutex = CreateMutexW(nullptr, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr,
            L"程序已经在运行中！\n\n请在系统托盘或任务栏中查找，或检查任务管理器。",
            L"运行太久了 - 重复启动",
            MB_ICONWARNING | MB_OK);
        return 0;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = WINDOW_CLASS;
    wc.hIcon = LoadIcon(nullptr, IDI_WARNING);
    RegisterClassExW(&wc);

    g_hWnd = CreateWindowExW(
        WS_EX_COMPOSITED,
        WINDOW_CLASS, WINDOW_TITLE,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, Scale(560), Scale(520),
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(g_hWnd, nShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hMutex) {
        ReleaseMutex(g_hMutex);
        CloseHandle(g_hMutex);
    }
    return 0;
}

#ifdef CONSOLE_BUILD
int wmain(int, wchar_t*[]) {
    return wWinMain(GetModuleHandleW(nullptr), nullptr, GetCommandLineW(), SW_SHOW);
}
#endif
