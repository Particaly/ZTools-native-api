#include <napi.h>
#include <windows.h>
#include <psapi.h>
#include <thread>
#include <chrono>
#include <string>
#include <atomic>

// 取消与自定义函数名冲突的Windows宏
#ifdef GetActiveWindow
#undef GetActiveWindow
#endif

// 全局变量 - 剪贴板监控
static HWND g_hwnd = NULL;
static std::thread g_messageThread;
static std::atomic<bool> g_isMonitoring(false);
static napi_threadsafe_function g_tsfn = nullptr;

// 全局变量 - 窗口监控
static HWINEVENTHOOK g_winEventHook = NULL;
static std::atomic<bool> g_isWindowMonitoring(false);
static napi_threadsafe_function g_windowTsfn = nullptr;
static std::thread g_windowMessageThread;

// 窗口过程（处理剪贴板消息）
LRESULT CALLBACK ClipboardWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLIPBOARDUPDATE:
            // 剪贴板变化，通知 JS
            if (g_tsfn != nullptr) {
                napi_call_threadsafe_function(g_tsfn, nullptr, napi_tsfn_nonblocking);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// 在主线程调用 JS 回调
void CallJs(napi_env env, napi_value js_callback, void* context, void* data) {
    if (env != nullptr && js_callback != nullptr) {
        napi_value global;
        napi_get_global(env, &global);
        napi_call_function(env, global, js_callback, 0, nullptr, nullptr);
    }
}

// 启动剪贴板监控
Napi::Value StartMonitor(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsFunction()) {
        Napi::TypeError::New(env, "Expected a callback function").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (g_isMonitoring) {
        Napi::Error::New(env, "Monitor already started").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (g_tsfn != nullptr) {
        Napi::Error::New(env, "Monitor already started").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // 创建线程安全函数
    napi_value callback = info[0];
    napi_value resource_name;
    napi_create_string_utf8(env, "ClipboardCallback", NAPI_AUTO_LENGTH, &resource_name);

    napi_create_threadsafe_function(
        env,
        callback,
        nullptr,
        resource_name,
        0,
        1,
        nullptr,
        nullptr,
        nullptr,
        CallJs,
        &g_tsfn
    );

    g_isMonitoring = true;

    // 启动消息循环线程
    g_messageThread = std::thread([]() {
        // 注册窗口类
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = ClipboardWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"ZToolsClipboardMonitor";

        if (!RegisterClassW(&wc)) {
            return;
        }

        // 创建隐藏的消息窗口
        g_hwnd = CreateWindowW(
            L"ZToolsClipboardMonitor",
            L"ZToolsClipboardMonitor",
            0, 0, 0, 0, 0,
            HWND_MESSAGE,  // 消息窗口
            NULL, GetModuleHandle(NULL), NULL
        );

        if (g_hwnd == NULL) {
            UnregisterClassW(L"ZToolsClipboardMonitor", GetModuleHandle(NULL));
            return;
        }

        // 注册剪贴板监听
        if (!AddClipboardFormatListener(g_hwnd)) {
            DestroyWindow(g_hwnd);
            UnregisterClassW(L"ZToolsClipboardMonitor", GetModuleHandle(NULL));
            return;
        }

        // 消息循环
        MSG msg;
        while (g_isMonitoring && GetMessageW(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // 清理
        RemoveClipboardFormatListener(g_hwnd);
        DestroyWindow(g_hwnd);
        UnregisterClassW(L"ZToolsClipboardMonitor", GetModuleHandle(NULL));
        g_hwnd = NULL;
    });

    return env.Undefined();
}

// 停止剪贴板监控
Napi::Value StopMonitor(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    g_isMonitoring = false;

    if (g_hwnd != NULL) {
        PostMessageW(g_hwnd, WM_QUIT, 0, 0);
    }

    if (g_messageThread.joinable()) {
        g_messageThread.join();
    }

    if (g_tsfn != nullptr) {
        napi_release_threadsafe_function(g_tsfn, napi_tsfn_release);
        g_tsfn = nullptr;
    }

    return env.Undefined();
}

// ==================== 窗口监控功能 ====================

// 窗口信息结构（用于线程安全传递）
struct WindowInfo {
    DWORD processId;
    std::string appName;
};

// 获取窗口信息的辅助函数
WindowInfo* GetWindowInfo(HWND hwnd) {
    if (hwnd == NULL) {
        return nullptr;
    }

    WindowInfo* info = new WindowInfo();

    // 获取进程 ID
    GetWindowThreadProcessId(hwnd, &info->processId);

    // 获取进程句柄
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, info->processId);
    if (hProcess) {
        // 获取可执行文件路径
        WCHAR path[MAX_PATH] = {0};
        if (GetModuleFileNameExW(hProcess, NULL, path, MAX_PATH)) {
            // 提取文件名（去掉路径）
            std::wstring fullPath(path);
            size_t lastSlash = fullPath.find_last_of(L"\\");
            std::wstring fileName = (lastSlash != std::wstring::npos)
                ? fullPath.substr(lastSlash + 1)
                : fullPath;

            // 去掉 .exe 扩展名
            size_t lastDot = fileName.find_last_of(L".");
            if (lastDot != std::wstring::npos) {
                fileName = fileName.substr(0, lastDot);
            }

            // 转换为 UTF-8
            int size = WideCharToMultiByte(CP_UTF8, 0, fileName.c_str(), -1, NULL, 0, NULL, NULL);
            if (size > 0) {
                info->appName.resize(size - 1);
                WideCharToMultiByte(CP_UTF8, 0, fileName.c_str(), -1, &info->appName[0], size, NULL, NULL);
            }
        }
        CloseHandle(hProcess);
    }

    return info;
}

// 在主线程调用 JS 回调（窗口监控）
void CallWindowJs(napi_env env, napi_value js_callback, void* context, void* data) {
    if (env != nullptr && js_callback != nullptr && data != nullptr) {
        WindowInfo* info = static_cast<WindowInfo*>(data);

        // 创建返回对象
        napi_value result;
        napi_create_object(env, &result);

        napi_value processId;
        napi_create_uint32(env, info->processId, &processId);
        napi_set_named_property(env, result, "processId", processId);

        napi_value appName;
        napi_create_string_utf8(env, info->appName.c_str(), NAPI_AUTO_LENGTH, &appName);
        napi_set_named_property(env, result, "appName", appName);

        // 调用回调
        napi_value global;
        napi_get_global(env, &global);
        napi_call_function(env, global, js_callback, 1, &result, nullptr);

        delete info;
    }
}

// 窗口事件回调
void CALLBACK WinEventProc(
    HWINEVENTHOOK hWinEventHook,
    DWORD event,
    HWND hwnd,
    LONG idObject,
    LONG idChild,
    DWORD dwEventThread,
    DWORD dwmsEventTime
) {
    // 只处理前台窗口切换事件
    if (event == EVENT_SYSTEM_FOREGROUND && g_windowTsfn != nullptr) {
        // 获取窗口信息
        WindowInfo* info = GetWindowInfo(hwnd);
        if (info != nullptr) {
            // 通过线程安全函数传递到 JS
            napi_call_threadsafe_function(g_windowTsfn, info, napi_tsfn_nonblocking);
        }
    }
}

// 窗口监控消息循环线程
void WindowMonitorThread() {
    // 在此线程中设置窗口事件钩子
    g_winEventHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_FOREGROUND,
        NULL,
        WinEventProc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

    if (g_winEventHook == NULL) {
        g_isWindowMonitoring = false;
        return;
    }

    // 运行消息循环
    MSG msg;
    while (g_isWindowMonitoring && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 清理钩子
    if (g_winEventHook != NULL) {
        UnhookWinEvent(g_winEventHook);
        g_winEventHook = NULL;
    }
}

// 启动窗口监控
Napi::Value StartWindowMonitor(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsFunction()) {
        Napi::TypeError::New(env, "Expected a callback function").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (g_isWindowMonitoring) {
        Napi::Error::New(env, "Window monitor already started").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Function callback = info[0].As<Napi::Function>();
    napi_value resource_name;
    napi_create_string_utf8(env, "WindowMonitor", NAPI_AUTO_LENGTH, &resource_name);

    // 创建线程安全函数
    napi_status status = napi_create_threadsafe_function(
        env,
        callback,
        nullptr,
        resource_name,
        0,
        1,
        nullptr,
        nullptr,
        nullptr,
        CallWindowJs,
        &g_windowTsfn
    );

    if (status != napi_ok) {
        Napi::Error::New(env, "Failed to create threadsafe function").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    g_isWindowMonitoring = true;

    // 启动消息循环线程（钩子将在线程内设置）
    g_windowMessageThread = std::thread(WindowMonitorThread);

    // 等待一小段时间确保线程启动
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 检查是否成功启动
    if (!g_isWindowMonitoring) {
        if (g_windowMessageThread.joinable()) {
            g_windowMessageThread.join();
        }
        napi_release_threadsafe_function(g_windowTsfn, napi_tsfn_release);
        g_windowTsfn = nullptr;
        Napi::Error::New(env, "Failed to set window event hook").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // 立即回调当前激活的窗口
    HWND currentWindow = GetForegroundWindow();
    if (currentWindow != NULL) {
        WindowInfo* info = GetWindowInfo(currentWindow);
        if (info != nullptr) {
            napi_call_threadsafe_function(g_windowTsfn, info, napi_tsfn_nonblocking);
        }
    }

    return env.Undefined();
}

// 停止窗口监控
Napi::Value StopWindowMonitor(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (!g_isWindowMonitoring) {
        return env.Undefined();
    }

    g_isWindowMonitoring = false;

    // 停止消息循环（钩子会在线程内自动清理）
    if (g_windowMessageThread.joinable()) {
        PostThreadMessage(GetThreadId(g_windowMessageThread.native_handle()), WM_QUIT, 0, 0);
        g_windowMessageThread.join();
    }

    // 释放线程安全函数
    if (g_windowTsfn != nullptr) {
        napi_release_threadsafe_function(g_windowTsfn, napi_tsfn_release);
        g_windowTsfn = nullptr;
    }

    return env.Undefined();
}

// ==================== 窗口信息获取 ====================


// 获取当前激活窗口
Napi::Value GetActiveWindowInfo(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // 获取前台窗口句柄
    HWND hwnd = GetForegroundWindow();
    if (hwnd == NULL) {
        return env.Null();
    }

    Napi::Object result = Napi::Object::New(env);

    // 获取进程 ID
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    result.Set("processId", Napi::Number::New(env, processId));

    // 获取进程句柄
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (hProcess) {
        // 获取可执行文件路径
        WCHAR path[MAX_PATH] = {0};
        if (GetModuleFileNameExW(hProcess, NULL, path, MAX_PATH)) {
            // 提取文件名（去掉路径）
            std::wstring fullPath(path);
            size_t lastSlash = fullPath.find_last_of(L"\\");
            std::wstring fileName = (lastSlash != std::wstring::npos)
                ? fullPath.substr(lastSlash + 1)
                : fullPath;

            // 去掉 .exe 扩展名
            size_t lastDot = fileName.find_last_of(L".");
            if (lastDot != std::wstring::npos) {
                fileName = fileName.substr(0, lastDot);
            }

            // 转换为 UTF-8
            int size = WideCharToMultiByte(CP_UTF8, 0, fileName.c_str(), -1, NULL, 0, NULL, NULL);
            if (size > 0) {
                std::string appNameUtf8(size - 1, 0);
                WideCharToMultiByte(CP_UTF8, 0, fileName.c_str(), -1, &appNameUtf8[0], size, NULL, NULL);
                result.Set("appName", Napi::String::New(env, appNameUtf8));
            }
        }
        CloseHandle(hProcess);
    }

    return result;
}

// 枚举窗口回调
struct EnumWindowsCallbackArgs {
    DWORD targetProcessId;
    HWND foundWindow;
};

BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    EnumWindowsCallbackArgs* args = (EnumWindowsCallbackArgs*)lParam;

    // 只处理可见的顶级窗口
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }

    // 跳过工具窗口
    if (GetWindowLong(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) {
        return TRUE;
    }

    // 获取窗口的进程 ID
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);

    // 找到匹配的进程
    if (processId == args->targetProcessId) {
        args->foundWindow = hwnd;
        return FALSE;  // 停止枚举
    }

    return TRUE;  // 继续枚举
}

// 激活窗口（使用 AttachThreadInput + 组合API 强制切换到前台）
Napi::Value ActivateWindow(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected processId number").ThrowAsJavaScriptException();
        return Napi::Boolean::New(env, false);
    }

    DWORD processId = info[0].As<Napi::Number>().Uint32Value();

    // 枚举所有窗口查找目标进程的窗口
    EnumWindowsCallbackArgs args = { processId, NULL };
    EnumWindows(EnumWindowsCallback, (LPARAM)&args);

    if (args.foundWindow == NULL) {
        return Napi::Boolean::New(env, false);
    }

    HWND hwnd = args.foundWindow;

    // 如果窗口最小化，先恢复
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }

    // 获取当前前台窗口的线程ID
    HWND foregroundWnd = GetForegroundWindow();
    DWORD foregroundThreadId = GetWindowThreadProcessId(foregroundWnd, NULL);
    DWORD targetThreadId = GetWindowThreadProcessId(hwnd, NULL);
    DWORD currentThreadId = GetCurrentThreadId();

    // 附加到前台窗口的线程（绕过Windows前台窗口限制）
    BOOL attached1 = FALSE;
    BOOL attached2 = FALSE;

    if (foregroundThreadId != targetThreadId) {
        attached1 = AttachThreadInput(foregroundThreadId, targetThreadId, TRUE);
    }
    if (currentThreadId != targetThreadId && currentThreadId != foregroundThreadId) {
        attached2 = AttachThreadInput(currentThreadId, targetThreadId, TRUE);
    }

    // 组合使用多个激活函数确保窗口切换到前台
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);

    // 分离线程输入
    if (attached1) {
        AttachThreadInput(foregroundThreadId, targetThreadId, FALSE);
    }
    if (attached2) {
        AttachThreadInput(currentThreadId, targetThreadId, FALSE);
    }

    // 验证是否成功
    HWND newForeground = GetForegroundWindow();
    return Napi::Boolean::New(env, newForeground == hwnd);
}

// 模块初始化
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("startMonitor", Napi::Function::New(env, StartMonitor));
    exports.Set("stopMonitor", Napi::Function::New(env, StopMonitor));
    exports.Set("startWindowMonitor", Napi::Function::New(env, StartWindowMonitor));
    exports.Set("stopWindowMonitor", Napi::Function::New(env, StopWindowMonitor));
    exports.Set("getActiveWindow", Napi::Function::New(env, GetActiveWindowInfo));
    exports.Set("activateWindow", Napi::Function::New(env, ActivateWindow));
    return exports;
}

NODE_API_MODULE(ztools_native, Init)
