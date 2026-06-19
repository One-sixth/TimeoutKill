#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>

// ========== 常量 ==========
constexpr const wchar_t* MUTEX_NAME = L"Global\\TimeoutKill_v1";
constexpr const wchar_t* WINDOW_CLASS = L"TimeoutKillClass";
constexpr const wchar_t* WINDOW_TITLE = L"TimeoutKill";
constexpr int BASE_DPI = 96;

// 控件 ID
constexpr int ID_EDIT_PROCESS  = 1001;
constexpr int ID_EDIT_COUNT    = 1002;
constexpr int ID_EDIT_INTERVAL = 1003;
constexpr int ID_BTN_TOGGLE    = 2001;
constexpr int ID_BTN_EXIT      = 2002;

// ========== 全局变量 ==========
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
static bool g_threadActive = false;
static int g_checkCount = 0;
static int g_maxChecks = 20;
static int g_intervalMin = 30;
static int g_dpi = 96;
static HFONT g_hFont = nullptr;
static HBRUSH g_hStatusBrush = nullptr;

// ========== DPI 缩放 ==========
static int Scale(int value) { return ::MulDiv(value, g_dpi, BASE_DPI); }
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

static std::wstring GetLogFile() { return GetExeDir() + L"\\TimeoutKill.log"; }

static void StartNewLog() {
    HANDLE hFile = CreateFileW(GetLogFile().c_str(), GENERIC_WRITE,
        0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        wchar_t bom = 0xFEFF;
        WriteFile(hFile, &bom, sizeof(bom), &written, nullptr);
        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t line[128];
        wsprintfW(line, L"[%04d-%02d-%02d %02d:%02d:%02d] ========== 程序启动 ==========\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        WriteFile(hFile, line, lstrlenW(line) * sizeof(wchar_t), &written, nullptr);
        CloseHandle(hFile);
    }
    if (g_hLog) SetWindowTextW(g_hLog, L"");
}

// 统一日志：写文件 + GUI 控件（GUI 模式下 g_hLog 非空才写控件）
static void AppendLog(const std::wstring& text) {
    // 写文件
    HANDLE hFile = CreateFileW(GetLogFile().c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        SetFilePointer(hFile, 0, nullptr, FILE_END);
        DWORD written;
        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t line[512];
        wsprintfW(line, L"[%04d-%02d-%02d %02d:%02d:%02d] %s\r\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, text.c_str());
        WriteFile(hFile, line, lstrlenW(line) * sizeof(wchar_t), &written, nullptr);
        CloseHandle(hFile);
    }
    // 写 GUI 控件
    if (g_hLog) {
        int len = GetWindowTextLengthW(g_hLog);
        SendMessageW(g_hLog, EM_SETSEL, len, len);
        std::wstring ln = text + L"\r\n";
        SendMessageW(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)ln.c_str());
        int lineCount = (int)SendMessageW(g_hLog, EM_GETLINECOUNT, 0, 0);
        if (lineCount > 200) {
            int cut = lineCount - 200;
            int pos = (int)SendMessageW(g_hLog, EM_LINEINDEX, cut, 0);
            SendMessageW(g_hLog, EM_SETSEL, 0, pos);
            SendMessageW(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)L"");
        }
        SendMessageW(g_hLog, EM_SCROLLCARET, 0, 0);
    }
}

// GUI 模式专用
static void SetStatus(const wchar_t* text, COLORREF color) {
    if (g_hStatus) {
        SetWindowTextW(g_hStatus, text);
        if (g_hStatusBrush) DeleteObject(g_hStatusBrush);
        g_hStatusBrush = CreateSolidBrush(color);
        InvalidateRect(g_hStatus, nullptr, TRUE);
    }
}

// ========== 进程检测 ==========
static bool IsProcessRunning(const std::wstring& name) {
    if (name.empty()) return false;
    std::wstring target = name;
    std::transform(target.begin(), target.end(), target.begin(), ::towlower);
    PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            std::wstring n(pe.szExeFile);
            std::transform(n.begin(), n.end(), n.begin(), ::towlower);
            if (n == target) { found = true; break; }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return found;
}

// ========== 杀进程（共享） ==========
static void KillProcessByName(const std::wstring& name) {
    std::wstring target = name;
    std::transform(target.begin(), target.end(), target.begin(), ::towlower);
    PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            std::wstring n(pe.szExeFile);
            std::transform(n.begin(), n.end(), n.begin(), ::towlower);
            if (n == target) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    TerminateProcess(hProc, 0);
                    CloseHandle(hProc);
                    wchar_t msg[128];
                    wsprintfW(msg, L"[结束] 已终止进程 PID=%lu", pe.th32ProcessID);
                    AppendLog(msg);
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
}

// ========== 统一监控线程 ==========
// 两种模式共用此函数：
//   GUI 模式：从控件读取参数
//   CLI 模式：通过参数传入
struct MonitorParams {
    std::wstring processName;
    int maxChecks;
    int intervalMin;
};

static DWORD WINAPI MonitorThread(LPVOID lpParam) {
    auto* p = (MonitorParams*)lpParam;
    std::wstring processName = p->processName;
    int maxChecks = p->maxChecks;
    int intervalMin = p->intervalMin;
    delete p;

    bool cli = processName.empty() == false && !g_hLog;
    // 如果 processName 为空，从控件读取（GUI 模式）
    if (processName.empty() && g_hEditProcess) {
        wchar_t buf[256] = {};
        SendMessageW(g_hEditProcess, WM_GETTEXT, 256, (LPARAM)buf);
        processName = buf;
        if (g_hEditCount) { wchar_t b[32]; GetWindowTextW(g_hEditCount, b, 32); maxChecks = _wtoi(b); if (maxChecks <= 0) maxChecks = 20; }
        if (g_hEditInterval) { wchar_t b[32]; GetWindowTextW(g_hEditInterval, b, 32); intervalMin = _wtoi(b); if (intervalMin <= 0) intervalMin = 30; }
    }

    if (processName.empty()) {
        AppendLog(L"[错误] 进程名为空");
        return 1;
    }

    wchar_t log[256];
    wsprintfW(log, L"[启动] 监控: %s, 次数=%d, 间隔=%d分钟", processName.c_str(), maxChecks, intervalMin);
    AppendLog(log);

    g_checkCount = 0;
    g_maxChecks = maxChecks;
    int waitSeconds = intervalMin * 60;

    while (g_threadActive) {
        for (int i = 0; i < waitSeconds && g_threadActive; ++i) {
            Sleep(1000);
        }
        if (!g_threadActive) break;

        // GUI 模式下动态读取最新参数（用户可能在界面上改了）
        if (!cli && g_hEditProcess) {
            wchar_t buf[256] = {};
            SendMessageW(g_hEditProcess, WM_GETTEXT, 256, (LPARAM)buf);
            if (buf[0]) processName = buf;
            if (g_hEditCount) { wchar_t b[32]; GetWindowTextW(g_hEditCount, b, 32); int v = _wtoi(b); if (v > 0) maxChecks = v; }
            if (g_hEditInterval) { wchar_t b[32]; GetWindowTextW(g_hEditInterval, b, 32); int v = _wtoi(b); if (v > 0) intervalMin = v; }
        }

        if (IsProcessRunning(processName)) {
            g_checkCount++;
            wsprintfW(log, L"[存活] %s (计数: %d/%d)", processName.c_str(), g_checkCount, maxChecks);
            AppendLog(log);

            if (g_checkCount >= maxChecks) {
                AppendLog(L"[超时] 达到最大计数，执行强制结束！");
                KillProcessByName(processName);
                g_checkCount = 0;
                AppendLog(L"[重置] 计数归零，继续监控下一轮");
            }
        } else {
            if (g_checkCount > 0) {
                wsprintfW(log, L"[消失] %s 已不在运行，计数重置为0", processName.c_str());
                AppendLog(log);
            }
            g_checkCount = 0;
        }
    }

    AppendLog(L"[停止] 监控线程已退出");
    return 0;
}

// ========== 命令行 ==========
static std::vector<std::wstring> SplitArgs(const std::wstring& cmdLine) {
    std::vector<std::wstring> args;
    std::wstring arg; bool inQuote = false;
    for (wchar_t c : cmdLine) {
        if (c == L'"') { inQuote = !inQuote; continue; }
        if (c == L' ' && !inQuote) { if (!arg.empty()) { args.push_back(arg); arg.clear(); } continue; }
        arg += c;
    }
    if (!arg.empty()) args.push_back(arg);
    return args;
}

static std::wstring GetHelpText() {
    return
        L"TimeoutKill - 进程超时自动终止工具\n\n"
        L"用法:\n"
        L"  TimeoutKill.exe                           启动 GUI 界面\n"
        L"  TimeoutKill.exe --help                    显示帮助\n"
        L"  TimeoutKill.exe start [选项]              命令行直接启动监控\n\n"
        L"选项:\n"
        L"  --process <名称>    目标进程名（不含路径，不区分大小写）\n"
        L"  --count <次数>      最大监视次数（默认 20）\n"
        L"  --interval <分钟>   每次检查间隔，单位分钟（默认 30）\n\n"
        L"示例:\n"
        L"  TimeoutKill.exe start --process chrome.exe --count 10 --interval 5\n"
        L"  每 5 分钟检查一次 chrome.exe，累计 10 次后终止\n\n"
        L"说明:\n"
        L"  命令行模式下按 Ctrl+C 可随时停止监控\n"
        L"  目标进程消失后计数归零，终止后自动继续监控下一轮\n";
}

static int RunHeadless(const std::wstring& processName, int maxChecks, int intervalMin) {
    AllocConsole();
    SetConsoleOutputCP(CP_UTF8);
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);

    wprintf(L"[TimeoutKill] 命令行模式启动\n");
    wprintf(L"  进程名: %s\n  最大次数: %d\n  检查间隔: %d 分钟\n  按 Ctrl+C 停止监控\n\n",
        processName.c_str(), maxChecks, intervalMin);

    StartNewLog();

    // 构建参数，启动统一监控线程
    auto* params = new MonitorParams{ processName, maxChecks, intervalMin };
    g_threadActive = true;
    g_hThread = CreateThread(nullptr, 0, MonitorThread, params, 0, nullptr);

    // 主线程等待：每秒检查 Ctrl+C
    while (g_threadActive) {
        if (GetAsyncKeyState(VK_CANCEL) & 0x8000) {
            wprintf(L"\n[TimeoutKill] 用户中断，退出监控\n");
            g_threadActive = false;
            break;
        }
        Sleep(1000);
    }

    if (g_hThread) { WaitForSingleObject(g_hThread, 3000); CloseHandle(g_hThread); g_hThread = nullptr; }
    fclose(stdout); fclose(stderr); FreeConsole();
    return 0;
}

// ========== 前置声明 ==========
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ========== 入口点 ==========
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nShow) {
    g_hInstance = hInstance;

    // ========== 命令行解析 ==========
    std::vector<std::wstring> args = SplitArgs(GetCommandLineW());
    bool hasCmdArgs = false, wantStart = false;
    std::wstring cliProcess;
    int cliCount = 20, cliInterval = 30;

    for (size_t i = 1; i < args.size(); ++i) {
        const std::wstring& a = args[i];
        if (a == L"--help" || a == L"-h") {
            bool needFree = !GetConsoleWindow(); if (needFree) AllocConsole();
            SetConsoleOutputCP(CP_UTF8);
            wprintf(L"%s", GetHelpText().c_str());
            if (needFree) { Sleep(500); FreeConsole(); }
            return 0;
        }
        if (a == L"start") { hasCmdArgs = true; wantStart = true; continue; }
        if (a == L"--process" && i + 1 < args.size()) { hasCmdArgs = true; cliProcess = args[++i]; continue; }
        if (a == L"--count" && i + 1 < args.size()) { hasCmdArgs = true; cliCount = _wtoi(args[++i].c_str()); if (cliCount <= 0) cliCount = 20; continue; }
        if (a == L"--interval" && i + 1 < args.size()) { hasCmdArgs = true; cliInterval = _wtoi(args[++i].c_str()); if (cliInterval <= 0) cliInterval = 30; continue; }
    }

    if (hasCmdArgs && !wantStart) {
        bool needFree = !GetConsoleWindow(); if (needFree) AllocConsole();
        SetConsoleOutputCP(CP_UTF8);
        wprintf(L"%s", GetHelpText().c_str());
        if (needFree) Sleep(500);
        return 0;
    }

    if (wantStart) {
        if (cliProcess.empty()) {
            bool needFree = !GetConsoleWindow(); if (needFree) AllocConsole();
            SetConsoleOutputCP(CP_UTF8);
            wprintf(L"[错误] start 模式必须指定 --process <进程名>\n\n%s", GetHelpText().c_str());
            if (needFree) Sleep(500);
            return 1;
        }
        return RunHeadless(cliProcess, cliCount, cliInterval);
    }

    // ========== GUI 模式 ==========
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_dpi = GetSystemDpi();

    g_hMutex = CreateMutexW(nullptr, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"程序已经在运行中！\n\n请在系统托盘或任务栏中查找，或检查任务管理器。",
            L"TimeoutKill", MB_ICONWARNING | MB_OK);
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

    g_hWnd = CreateWindowExW(0, WINDOW_CLASS, WINDOW_TITLE,
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

    if (g_hMutex) { ReleaseMutex(g_hMutex); CloseHandle(g_hMutex); }
    return 0;
}

// ========== 窗口过程（GUI 模式专用） ==========
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        int fontHeight = -Scale(14);
        g_hFont = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft YaHei UI");
        int x = Scale(20), y = Scale(12), w = Scale(500);
        HWND hTmp;

        hTmp = CreateWindowW(L"static", L"目标进程名（不含路径）：", WS_CHILD | WS_VISIBLE,
            x, y + Scale(3), w, Scale(22), hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTmp, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hEditProcess = CreateWindowExW(WS_EX_CLIENTEDGE, L"edit", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            x, y + Scale(25), w, Scale(26), hWnd, (HMENU)(INT_PTR)ID_EDIT_PROCESS, g_hInstance, nullptr);
        SendMessageW(g_hEditProcess, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hEditProcess, EM_SETLIMITTEXT, 255, 0);
        y += Scale(62);

        hTmp = CreateWindowW(L"static", L"最大监视次数：", WS_CHILD | WS_VISIBLE,
            x, y + Scale(3), Scale(110), Scale(22), hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTmp, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hEditCount = CreateWindowExW(WS_EX_CLIENTEDGE, L"edit", L"20",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
            x + Scale(112), y, Scale(60), Scale(26), hWnd, (HMENU)(INT_PTR)ID_EDIT_COUNT, g_hInstance, nullptr);
        SendMessageW(g_hEditCount, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        hTmp = CreateWindowW(L"static", L"次", WS_CHILD | WS_VISIBLE,
            x + Scale(178), y + Scale(3), Scale(20), Scale(22), hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTmp, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        hTmp = CreateWindowW(L"static", L"每次等待：", WS_CHILD | WS_VISIBLE,
            x + Scale(230), y + Scale(3), Scale(85), Scale(22), hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTmp, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hEditInterval = CreateWindowExW(WS_EX_CLIENTEDGE, L"edit", L"30",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
            x + Scale(317), y, Scale(60), Scale(26), hWnd, (HMENU)(INT_PTR)ID_EDIT_INTERVAL, g_hInstance, nullptr);
        SendMessageW(g_hEditInterval, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        hTmp = CreateWindowW(L"static", L"分钟", WS_CHILD | WS_VISIBLE,
            x + Scale(383), y + Scale(3), Scale(30), Scale(22), hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTmp, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        y += Scale(36);

        g_hLabelCount = CreateWindowW(L"static", L"0 / 20",
            WS_CHILD | WS_VISIBLE | SS_CENTER, x + Scale(340), y, Scale(160), Scale(22),
            hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(g_hLabelCount, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        hTmp = CreateWindowW(L"static", L"当前计数：", WS_CHILD | WS_VISIBLE,
            x, y + Scale(2), Scale(85), Scale(22), hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTmp, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        y += Scale(30);

        HWND hInfo = CreateWindowW(L"static",
            L"目标存活则计数+1，消失则归零。\n累计达到最大次数后自动结束目标进程。",
            WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, Scale(36), hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hInfo, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        y += Scale(44);

        g_hBtnToggle = CreateWindowW(L"button", L"开始监控", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, y, Scale(130), Scale(36), hWnd, (HMENU)(INT_PTR)ID_BTN_TOGGLE, g_hInstance, nullptr);
        SendMessageW(g_hBtnToggle, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hBtnExit = CreateWindowW(L"button", L"退出程序", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x + Scale(150), y, Scale(130), Scale(36), hWnd, (HMENU)(INT_PTR)ID_BTN_EXIT, g_hInstance, nullptr);
        SendMessageW(g_hBtnExit, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        y += Scale(46);

        g_hStatus = CreateWindowW(L"static", L"就绪 - 请输入进程名", WS_CHILD | WS_VISIBLE | SS_CENTER,
            x, y, w, Scale(28), hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        y += Scale(36);

        hTmp = CreateWindowW(L"static", L"运行日志：", WS_CHILD | WS_VISIBLE,
            x, y, Scale(100), Scale(20), hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(hTmp, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        y += Scale(20);

        g_hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"edit", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            x, y, w, Scale(160), hWnd, nullptr, g_hInstance, nullptr);
        SendMessageW(g_hLog, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hLog, EM_SETLIMITTEXT, 32 * 1024, 0);
        SendMessageW(g_hLog, 0x0447, 0, 0);  // EM_SETUNDOLIMIT=0

        // 读取上次设置
        wchar_t cfgPath[MAX_PATH]; wsprintfW(cfgPath, L"%s\\TimeoutKill.cfg", GetExeDir().c_str());
        wchar_t saved[256] = {};
        GetPrivateProfileStringW(L"Main", L"ProcessName", L"", saved, 256, cfgPath);
        if (lstrlenW(saved) > 0) SetWindowTextW(g_hEditProcess, saved);
        GetPrivateProfileStringW(L"Main", L"MaxChecks", L"20", saved, 256, cfgPath);
        SetWindowTextW(g_hEditCount, saved);
        GetPrivateProfileStringW(L"Main", L"IntervalMin", L"30", saved, 256, cfgPath);
        SetWindowTextW(g_hEditInterval, saved);

        StartNewLog();
        break;
    }

    case WM_DPICHANGED: {
        g_dpi = HIWORD(wParam);
        if (g_hFont) DeleteObject(g_hFont);
        g_hFont = CreateFontW(-Scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft YaHei UI");
        EnumChildWindows(hWnd, [](HWND h, LPARAM p) -> BOOL { SendMessageW(h, WM_SETFONT, p, TRUE); return TRUE; }, (LPARAM)g_hFont);
        if (lParam) { RECT* rc = (RECT*)lParam; SetWindowPos(hWnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE); }
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_BTN_TOGGLE:
            if (g_threadActive) {
                // 停止
                g_threadActive = false;
                if (g_hThread) { WaitForSingleObject(g_hThread, 3000); CloseHandle(g_hThread); g_hThread = nullptr; }
                SetStatus(L"已停止", RGB(150, 150, 150));
                SetWindowTextW(g_hBtnToggle, L"开始监控");
            } else {
                // 读取参数并校验
                wchar_t procBuf[256] = {};
                GetWindowTextW(g_hEditProcess, procBuf, 256);
                if (!procBuf[0]) { MessageBoxW(hWnd, L"请先输入目标进程名！", L"提示", MB_ICONINFORMATION); break; }
                wchar_t cBuf[32], iBuf[32];
                GetWindowTextW(g_hEditCount, cBuf, 32); int cnt = _wtoi(cBuf);
                GetWindowTextW(g_hEditInterval, iBuf, 32); int intv = _wtoi(iBuf);
                if (cnt <= 0 || intv <= 0) { MessageBoxW(hWnd, L"监视次数和等待时间必须大于0！", L"提示", MB_ICONWARNING); break; }

                // 保存配置
                wchar_t cfgPath[MAX_PATH]; wsprintfW(cfgPath, L"%s\\TimeoutKill.cfg", GetExeDir().c_str());
                WritePrivateProfileStringW(L"Main", L"ProcessName", procBuf, cfgPath);
                wchar_t tmp[32];
                wsprintfW(tmp, L"%d", cnt); WritePrivateProfileStringW(L"Main", L"MaxChecks", tmp, cfgPath);
                wsprintfW(tmp, L"%d", intv); WritePrivateProfileStringW(L"Main", L"IntervalMin", tmp, cfgPath);

                AppendLog(L"开始监控");
                StartNewLog();

                // 启动统一监控线程（传空 processName，线程会从控件读取）
                auto* params = new MonitorParams{ L"", 0, 0 };
                g_threadActive = true;
                g_hThread = CreateThread(nullptr, 0, MonitorThread, params, 0, nullptr);
                SetStatus(L"监控中", RGB(50, 180, 50));
                SetWindowTextW(g_hBtnToggle, L"停止监控");
            }
            break;

        case ID_BTN_EXIT:
            if (g_threadActive) { g_threadActive = false; if (g_hThread) { WaitForSingleObject(g_hThread, 3000); CloseHandle(g_hThread); } }
            DestroyWindow(hWnd);
            break;

        case ID_EDIT_COUNT:
        case ID_EDIT_INTERVAL:
            if (HIWORD(wParam) == EN_CHANGE) {
                wchar_t b1[32];
                GetWindowTextW(g_hEditCount, b1, 32);
                int c = _wtoi(b1);
                if (g_hLabelCount) { wchar_t buf[64]; wsprintfW(buf, L"%d / %d", 0, c > 0 ? c : 20); SetWindowTextW(g_hLabelCount, buf); }
            }
            break;
        }
        break;

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        if ((HWND)lParam == g_hStatus && g_hStatusBrush) { SetBkColor(hdc, RGB(240, 240, 240)); return (LRESULT)g_hStatusBrush; }
        break;
    }

    case WM_CLOSE:
        if (g_threadActive) { g_threadActive = false; if (g_hThread) { WaitForSingleObject(g_hThread, 3000); CloseHandle(g_hThread); } }
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
