import Foundation
import Cocoa
import ApplicationServices

// C 风格回调函数类型（无参数）
public typealias ClipboardCallback = @convention(c) () -> Void

// C 风格回调函数类型（带JSON字符串参数）
public typealias WindowCallback = @convention(c) (UnsafePointer<CChar>?) -> Void

// 全局监控状态
private var clipboardMonitorQueue: DispatchQueue?
private var isClipboardMonitoring = false

// 窗口监控状态
private var windowMonitorObserver: NSObjectProtocol?
private var isWindowMonitoring = false

// MARK: - Clipboard Monitor

/// 启动剪贴板监控
/// - Parameter callback: 当剪贴板变化时调用的 C 回调函数
@_cdecl("startClipboardMonitor")
public func startClipboardMonitor(_ callback: ClipboardCallback?) {
    guard let callback = callback else {
        print("Error: callback is nil")
        return
    }

    // 防止重复启动
    guard !isClipboardMonitoring else {
        print("Warning: Clipboard monitor already running")
        return
    }

    isClipboardMonitoring = true
    let pasteboard = NSPasteboard.general
    var changeCount = pasteboard.changeCount

    // 创建专用队列
    clipboardMonitorQueue = DispatchQueue(label: "com.ztools.clipboard.monitor", qos: .utility)

    clipboardMonitorQueue?.async {
        print("Clipboard monitor started")

        while isClipboardMonitoring {
            usleep(500_000) // 0.5 秒检查一次

            let currentCount = pasteboard.changeCount
            if currentCount != changeCount {
                changeCount = currentCount

                // 只通知变化事件，不传递内容
                callback()
            }
        }

        print("Clipboard monitor stopped")
    }
}

/// 停止剪贴板监控
@_cdecl("stopClipboardMonitor")
public func stopClipboardMonitor() {
    isClipboardMonitoring = false
    clipboardMonitorQueue = nil
}

// MARK: - Window Management

/// 获取当前激活窗口的信息（JSON 格式）
/// - Returns: JSON 字符串包含 appName 和 bundleId，需要调用者 free
@_cdecl("getActiveWindow")
public func getActiveWindow() -> UnsafeMutablePointer<CChar>? {
    // 获取当前激活的应用
    guard let frontmostApp = NSWorkspace.shared.frontmostApplication else {
        return strdup("{\"error\":\"No frontmost application\"}")
    }

    let appName = frontmostApp.localizedName ?? "Unknown"
    let bundleId = frontmostApp.bundleIdentifier ?? "unknown.bundle.id"

    // 构建 JSON 字符串
    let jsonString = """
    {"appName":"\(escapeJSON(appName))","bundleId":"\(escapeJSON(bundleId))"}
    """

    return strdup(jsonString)
}

/// 根据 bundleId 激活应用窗口
/// - Parameter bundleId: 应用的 bundle identifier
/// - Returns: 是否激活成功 (1: 成功, 0: 失败)
@_cdecl("activateWindow")
public func activateWindow(_ bundleId: UnsafePointer<CChar>?) -> Int32 {
    guard let bundleId = bundleId else {
        return 0
    }

    let bundleIdString = String(cString: bundleId)

    // 查找并激活应用
    let runningApps = NSRunningApplication.runningApplications(withBundleIdentifier: bundleIdString)
    if let app = runningApps.first {
        let success = app.activate(options: [.activateAllWindows, .activateIgnoringOtherApps])
        return success ? 1 : 0
    }

    return 0
}

// MARK: - Window Monitor

/// 启动窗口激活监控
/// - Parameter callback: 窗口切换时调用的回调，传递JSON字符串
@_cdecl("startWindowMonitor")
public func startWindowMonitor(_ callback: WindowCallback?) {
    guard let callback = callback else {
        print("Error: window callback is nil")
        return
    }

    // 防止重复启动
    guard !isWindowMonitoring else {
        print("Warning: Window monitor already running")
        return
    }

    isWindowMonitoring = true

    // 监听应用激活通知
    windowMonitorObserver = NSWorkspace.shared.notificationCenter.addObserver(
        forName: NSWorkspace.didActivateApplicationNotification,
        object: nil,
        queue: .main
    ) { notification in
        guard isWindowMonitoring else { return }

        // 获取激活的应用
        if let app = notification.userInfo?[NSWorkspace.applicationUserInfoKey] as? NSRunningApplication {
            let appName = app.localizedName ?? "Unknown"
            let bundleId = app.bundleIdentifier ?? "unknown.bundle.id"

            // 构建JSON字符串
            let jsonString = """
            {"appName":"\(escapeJSON(appName))","bundleId":"\(escapeJSON(bundleId))"}
            """

            // 调用回调
            jsonString.withCString { cString in
                callback(cString)
            }
        }
    }

    print("Window monitor started")
}

/// 停止窗口激活监控
@_cdecl("stopWindowMonitor")
public func stopWindowMonitor() {
    guard isWindowMonitoring else { return }

    isWindowMonitoring = false

    // 移除观察者
    if let observer = windowMonitorObserver {
        NSWorkspace.shared.notificationCenter.removeObserver(observer)
        windowMonitorObserver = nil
    }

    print("Window monitor stopped")
}


// MARK: - Helper Functions

/// 辅助函数：转义 JSON 字符串
private func escapeJSON(_ string: String) -> String {
    return string
        .replacingOccurrences(of: "\\", with: "\\\\")
        .replacingOccurrences(of: "\"", with: "\\\"")
        .replacingOccurrences(of: "\n", with: "\\n")
        .replacingOccurrences(of: "\r", with: "\\r")
        .replacingOccurrences(of: "\t", with: "\\t")
}
