#include <napi.h>
#define NOMINMAX  // 防止 Windows.h 定义 min/max 宏
#include <windows.h>
#include <windowsx.h>  // For GET_X_LPARAM, GET_Y_LPARAM
#include <psapi.h>
#include <thread>
#include <chrono>
#include <string>
#include <atomic>
#include <algorithm>   // For std::min, std::max

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

// 全局变量 - 区域截图
static HWND g_screenshotOverlayWindow = NULL;
static std::atomic<bool> g_isCapturing(false);
static napi_threadsafe_function g_screenshotTsfn = nullptr;
static std::thread g_screenshotThread;
static POINT g_selectionStart = {0, 0};
static POINT g_selectionEnd = {0, 0};
static bool g_isSelecting = false;

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

// ==================== 区域截图功能 ====================

// 截图结果结构
struct ScreenshotResult {
    bool success;
    int width;
    int height;
};

// 保存截图到剪贴板
bool SaveBitmapToClipboard(HBITMAP hBitmap) {
    if (!OpenClipboard(NULL)) {
        return false;
    }

    EmptyClipboard();
    
    // 复制 HBITMAP（剪贴板会负责释放）
    BITMAP bm;
    GetObject(hBitmap, sizeof(BITMAP), &bm);
    
    HBITMAP hBitmapCopy = (HBITMAP)CopyImage(hBitmap, IMAGE_BITMAP, bm.bmWidth, bm.bmHeight, LR_COPYRETURNORG);
    
    SetClipboardData(CF_BITMAP, hBitmapCopy);
    CloseClipboard();
    
    return true;
}

// 截取屏幕区域并保存到剪贴板
ScreenshotResult* CaptureScreenRegion(int x, int y, int width, int height) {
    ScreenshotResult* result = new ScreenshotResult();
    result->success = false;
    result->width = width;
    result->height = height;

    if (width <= 0 || height <= 0) {
        return result;
    }

    // 获取屏幕 DC
    HDC screenDC = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(screenDC);
    
    // 创建位图
    HBITMAP bitmap = CreateCompatibleBitmap(screenDC, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, bitmap);
    
    // 复制屏幕内容
    BitBlt(memDC, 0, 0, width, height, screenDC, x, y, SRCCOPY);
    
    // 保存到剪贴板
    result->success = SaveBitmapToClipboard(bitmap);
    
    // 清理
    SelectObject(memDC, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDC);
    ReleaseDC(NULL, screenDC);
    
    return result;
}

// 在主线程调用 JS 回调（截图完成）
void CallScreenshotJs(napi_env env, napi_value js_callback, void* context, void* data) {
    if (env != nullptr && js_callback != nullptr && data != nullptr) {
        ScreenshotResult* result = static_cast<ScreenshotResult*>(data);
        
        // 创建返回对象
        napi_value resultObj;
        napi_create_object(env, &resultObj);
        
        napi_value success;
        napi_get_boolean(env, result->success, &success);
        napi_set_named_property(env, resultObj, "success", success);
        
        if (result->success) {
            napi_value width;
            napi_create_int32(env, result->width, &width);
            napi_set_named_property(env, resultObj, "width", width);
            
            napi_value height;
            napi_create_int32(env, result->height, &height);
            napi_set_named_property(env, resultObj, "height", height);
        }
        
        // 调用回调
        napi_value global;
        napi_get_global(env, &global);
        napi_call_function(env, global, js_callback, 1, &resultObj, nullptr);
        
        delete result;
    }
}

// 使用 UpdateLayeredWindow 绘制选区遮罩（支持像素级透明度）
void DrawSelectionOverlay(HWND hwnd) {
    // 获取窗口尺寸
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;
    
    // 创建内存 DC 和位图
    HDC screenDC = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP memBitmap = CreateCompatibleBitmap(screenDC, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
    
    // 创建 32 位 BGRA 位图用于 alpha 通道
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;  // 负值表示自顶向下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    void* pvBits = nullptr;
    HBITMAP hbmAlpha = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0);
    HBITMAP oldAlphaBitmap = (HBITMAP)SelectObject(memDC, hbmAlpha);
    
    // 填充位图数据
    BYTE* pixels = (BYTE*)pvBits;
    
    if (g_isSelecting) {
        // 计算选区矩形
        int selLeft = (std::min)(g_selectionStart.x, g_selectionEnd.x);
        int selTop = (std::min)(g_selectionStart.y, g_selectionEnd.y);
        int selRight = (std::max)(g_selectionStart.x, g_selectionEnd.x);
        int selBottom = (std::max)(g_selectionStart.y, g_selectionEnd.y);
        
        // 填充每个像素
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int offset = (y * width + x) * 4;
                
                // 判断是否在选区内
                bool inSelection = (x >= selLeft && x < selRight && y >= selTop && y < selBottom);
                
                if (inSelection) {
                    // 选区内：完全透明
                    pixels[offset + 0] = 0;  // B
                    pixels[offset + 1] = 0;  // G
                    pixels[offset + 2] = 0;  // R
                    pixels[offset + 3] = 0;  // A (完全透明)
                } else {
                    // 选区外：半透明黑色
                    pixels[offset + 0] = 0;  // B
                    pixels[offset + 1] = 0;  // G
                    pixels[offset + 2] = 0;  // R
                    pixels[offset + 3] = 128;  // A (50% 透明)
                }
            }
        }
        
        // 绘制选区边框
        HPEN borderPen = CreatePen(PS_SOLID, 2, RGB(0, 120, 215));
        HPEN oldPen = (HPEN)SelectObject(memDC, borderPen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
        
        Rectangle(memDC, selLeft, selTop, selRight, selBottom);
        
        SelectObject(memDC, oldPen);
        SelectObject(memDC, oldBrush);
        DeleteObject(borderPen);
    } else {
        // 没有选区时，整个屏幕半透明黑色
        for (int i = 0; i < width * height * 4; i += 4) {
            pixels[i + 0] = 0;    // B
            pixels[i + 1] = 0;    // G
            pixels[i + 2] = 0;    // R
            pixels[i + 3] = 128;  // A
        }
    }
    
    // 使用 UpdateLayeredWindow 更新窗口
    POINT ptSrc = {0, 0};
    POINT ptDst = {0, 0};
    SIZE sizeWnd = {width, height};
    BLENDFUNCTION blend = {0};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    
    UpdateLayeredWindow(hwnd, screenDC, &ptDst, &sizeWnd, memDC, &ptSrc, 0, &blend, ULW_ALPHA);
    
    // 清理
    SelectObject(memDC, oldAlphaBitmap);
    DeleteObject(hbmAlpha);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    ReleaseDC(NULL, screenDC);
}

// 截图遮罩窗口过程
LRESULT CALLBACK ScreenshotOverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_LBUTTONDOWN: {
            // 记录起始点
            g_selectionStart.x = GET_X_LPARAM(lParam);
            g_selectionStart.y = GET_Y_LPARAM(lParam);
            g_selectionEnd = g_selectionStart;
            g_isSelecting = true;
            SetCapture(hwnd);
            DrawSelectionOverlay(hwnd);  // 立即更新显示
            return 0;
        }
        
        case WM_MOUSEMOVE: {
            if (g_isSelecting) {
                // 更新终点
                g_selectionEnd.x = GET_X_LPARAM(lParam);
                g_selectionEnd.y = GET_Y_LPARAM(lParam);
                DrawSelectionOverlay(hwnd);  // 立即更新显示
            }
            return 0;
        }
        
        case WM_LBUTTONUP: {
            if (g_isSelecting) {
                g_isSelecting = false;
                ReleaseCapture();
                
                // 计算选区
                int x = (std::min)(g_selectionStart.x, g_selectionEnd.x);
                int y = (std::min)(g_selectionStart.y, g_selectionEnd.y);
                int width = abs(g_selectionEnd.x - g_selectionStart.x);
                int height = abs(g_selectionEnd.y - g_selectionStart.y);
                
                // 隐藏窗口
                ShowWindow(hwnd, SW_HIDE);
                Sleep(100);  // 等待窗口完全隐藏
                
                // 执行截图
                ScreenshotResult* result = CaptureScreenRegion(x, y, width, height);
                
                // 通过线程安全函数回调 JS
                if (g_screenshotTsfn != nullptr) {
                    napi_call_threadsafe_function(g_screenshotTsfn, result, napi_tsfn_nonblocking);
                }
                
                // 关闭窗口
                DestroyWindow(hwnd);
            }
            return 0;
        }
        
        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                // ESC 取消截图
                g_isSelecting = false;
                
                // 回调失败结果
                if (g_screenshotTsfn != nullptr) {
                    ScreenshotResult* result = new ScreenshotResult();
                    result->success = false;
                    result->width = 0;
                    result->height = 0;
                    napi_call_threadsafe_function(g_screenshotTsfn, result, napi_tsfn_nonblocking);
                }
                
                DestroyWindow(hwnd);
            }
            return 0;
        }
        
        case WM_DESTROY: {
            g_screenshotOverlayWindow = NULL;
            g_isCapturing = false;
            PostQuitMessage(0);
            return 0;
        }
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// 截图线程（创建遮罩窗口）
void ScreenshotCaptureThread() {
    // 设置 DPI 感知（Per-Monitor V2）
    // 这样可以正确处理高 DPI 显示器
    typedef DPI_AWARENESS_CONTEXT (WINAPI *SetThreadDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        SetThreadDpiAwarenessContextProc setDpiProc = 
            (SetThreadDpiAwarenessContextProc)GetProcAddress(user32, "SetThreadDpiAwarenessContext");
        if (setDpiProc) {
            setDpiProc(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }
    
    // 注册窗口类
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = ScreenshotOverlayWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_CROSS);  // 十字光标
    wc.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
    wc.lpszClassName = L"ZToolsScreenshotOverlay";
    
    if (!RegisterClassExW(&wc)) {
        g_isCapturing = false;
        return;
    }
    
    // 获取虚拟屏幕尺寸（支持多显示器）
    int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int screenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    
    // 创建全屏分层窗口（覆盖所有显示器）
    g_screenshotOverlayWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        L"ZToolsScreenshotOverlay",
        L"Screenshot Overlay",
        WS_POPUP,
        screenX, screenY, screenWidth, screenHeight,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );
    
    if (g_screenshotOverlayWindow == NULL) {
        UnregisterClassW(L"ZToolsScreenshotOverlay", GetModuleHandle(NULL));
        g_isCapturing = false;
        return;
    }
    
    // 显示窗口
    ShowWindow(g_screenshotOverlayWindow, SW_SHOW);
    SetForegroundWindow(g_screenshotOverlayWindow);
    
    // 初始绘制（没有选区时的半透明遮罩）
    DrawSelectionOverlay(g_screenshotOverlayWindow);
    
    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // 清理
    UnregisterClassW(L"ZToolsScreenshotOverlay", GetModuleHandle(NULL));
    g_isCapturing = false;
}

// 启动区域截图
Napi::Value StartRegionCapture(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (g_isCapturing) {
        Napi::Error::New(env, "Screenshot already in progress").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    
    // 可选的回调函数
    if (info.Length() > 0 && info[0].IsFunction()) {
        Napi::Function callback = info[0].As<Napi::Function>();
        napi_value resource_name;
        napi_create_string_utf8(env, "ScreenshotCallback", NAPI_AUTO_LENGTH, &resource_name);
        
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
            CallScreenshotJs,
            &g_screenshotTsfn
        );
        
        if (status != napi_ok) {
            Napi::Error::New(env, "Failed to create threadsafe function").ThrowAsJavaScriptException();
            return env.Undefined();
        }
    }
    
    g_isCapturing = true;
    g_isSelecting = false;
    
    // 启动截图线程
    g_screenshotThread = std::thread(ScreenshotCaptureThread);
    g_screenshotThread.detach();
    
    return env.Undefined();
}

// ==================== 键盘模拟功能 ====================

// 模拟粘贴操作（Ctrl + V）
Napi::Value SimulatePaste(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // 创建输入事件数组
    INPUT inputs[4] = {};

    // 1. 按下 Ctrl 键
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[0].ki.dwFlags = 0;

    // 2. 按下 V 键
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'V';
    inputs[1].ki.dwFlags = 0;

    // 3. 释放 V 键
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

    // 4. 释放 Ctrl 键
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    // 发送输入事件
    UINT result = SendInput(4, inputs, sizeof(INPUT));

    // 返回是否成功（应该发送了4个事件）
    return Napi::Boolean::New(env, result == 4);
}

// 模块初始化
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("startMonitor", Napi::Function::New(env, StartMonitor));
    exports.Set("stopMonitor", Napi::Function::New(env, StopMonitor));
    exports.Set("startWindowMonitor", Napi::Function::New(env, StartWindowMonitor));
    exports.Set("stopWindowMonitor", Napi::Function::New(env, StopWindowMonitor));
    exports.Set("getActiveWindow", Napi::Function::New(env, GetActiveWindowInfo));
    exports.Set("activateWindow", Napi::Function::New(env, ActivateWindow));
    exports.Set("simulatePaste", Napi::Function::New(env, SimulatePaste));
    exports.Set("startRegionCapture", Napi::Function::New(env, StartRegionCapture));
    return exports;
}

NODE_API_MODULE(ztools_native, Init)
