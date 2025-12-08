# ZTools Native API

macOS 和 Windows 原生 API 的 Node.js 封装，使用 Swift + Win32 API + Node-API (N-API) 实现。

## ✨ 功能

1. **剪贴板变动监控** - 实时监听剪贴板内容变化
2. **窗口激活监控** - 实时监听窗口切换事件
3. **获取当前窗口** - 获取当前激活窗口的应用名和标识符
4. **设置激活窗口** - 根据标识符激活指定应用
5. **区域截图** - 选区截图并自动保存到剪贴板（Windows）

## 🔧 系统要求

### macOS
- macOS 10.15+
- Node.js 16.0+
- Swift 5.0+
- Xcode Command Line Tools

### Windows
- Windows 10+
- Node.js 16.0+
- Visual Studio Build Tools 或 Visual Studio 2019+

## 📦 安装

```bash
npm install
npm run build
```

## 🚀 使用方法

### 基础示例

```javascript
const { ClipboardMonitor, WindowMonitor, WindowManager } = require('ztools-native-api');

// 1. 剪贴板变动监控（跨平台一致）
const clipboardMonitor = new ClipboardMonitor();
clipboardMonitor.start(() => {
  console.log('剪贴板变化了！');
});

// 停止监控
clipboardMonitor.stop();

// 2. 窗口激活监控（实时监听窗口切换）
const windowMonitor = new WindowMonitor();
windowMonitor.start((windowInfo) => {
  console.log('窗口切换:', windowInfo);
  // macOS => { appName: 'Safari', bundleId: 'com.apple.Safari' }
  // Windows => { appName: 'chrome', processId: 12345 }
});

// 停止监控
windowMonitor.stop();

// 3. 获取当前激活窗口
const activeWindow = WindowManager.getActiveWindow();
console.log(activeWindow);
// macOS => { appName: '终端', bundleId: 'com.apple.Terminal' }
// Windows => { appName: 'chrome', processId: 12345 }

// 3. 激活指定窗口
// macOS: 使用 bundleId (string)
WindowManager.activateWindow('com.apple.Safari');

// Windows: 使用 processId (number)
WindowManager.activateWindow(12345);

// 4. 区域截图（仅 Windows）
const { ScreenCapture } = require('ztools-native-api');

ScreenCapture.start((result) => {
  if (result.success) {
    console.log(`截图成功！尺寸: ${result.width} x ${result.height}`);
    console.log('截图已保存到剪贴板，可按 Ctrl+V 粘贴');
  } else {
    console.log('截图已取消');
  }
});
// 操作：拖拽选择区域后释放鼠标，或按 ESC 取消
```

### 跨平台兼容示例

```javascript
const { WindowManager } = require('ztools-native-api');

// 获取当前窗口
const current = WindowManager.getActiveWindow();

// 跨平台激活窗口
if (WindowManager.getPlatform() === 'darwin') {
  // macOS
  WindowManager.activateWindow('com.apple.Safari');
} else if (WindowManager.getPlatform() === 'win32') {
  // Windows - 使用之前获取的 processId
  WindowManager.activateWindow(current.processId);
}
```

## 📖 API

### `ClipboardMonitor`

#### `start(callback)`
启动剪贴板监控
- **参数**: `callback()` - 剪贴板变化时的回调函数（无参数，只通知变化事件）
- **跨平台**: ✅ 一致

#### `stop()`
停止剪贴板监控
- **跨平台**: ✅ 一致

#### `isMonitoring`
只读属性，是否正在监控
- **跨平台**: ✅ 一致

---

### `WindowMonitor`

#### `start(callback)`
启动窗口激活监控
- **参数**: `callback(windowInfo)` - 窗口切换时的回调函数
  - **macOS**: `{appName: string, bundleId: string}`
  - **Windows**: `{appName: string, processId: number}`
- **跨平台**: ✅ API一致，返回值字段不同

#### `stop()`
停止窗口监控
- **跨平台**: ✅ 一致

#### `isMonitoring`
只读属性，是否正在监控
- **跨平台**: ✅ 一致

---

### `WindowManager`

#### `WindowManager.getActiveWindow()`
获取当前激活窗口
- **返回值**:
  - **macOS**: `{appName: string, bundleId: string} | null`
  - **Windows**: `{appName: string, processId: number} | null`

**示例**:
```javascript
// macOS
{ appName: 'Safari', bundleId: 'com.apple.Safari' }

// Windows
{ appName: 'chrome', processId: 12345 }
```

#### `WindowManager.activateWindow(identifier)`
激活指定应用窗口
- **参数**:
  - **macOS**: `bundleId` (string) - Bundle Identifier
  - **Windows**: `processId` (number) - 进程 ID
- **返回**: `boolean` - 是否激活成功

**示例**:
```javascript
// macOS
WindowManager.activateWindow('com.apple.Safari');

// Windows
WindowManager.activateWindow(12345);
```

#### `WindowManager.getPlatform()`
获取当前平台
- **返回**: `'darwin' | 'win32'`

---

### `ScreenCapture`

#### `ScreenCapture.start(callback)`
启动区域截图（仅 Windows）
- **参数**: `callback(result)` - 截图完成时的回调函数
  - `result.success` (boolean) - 是否成功截图
  - `result.width` (number) - 截图宽度（成功时）
  - `result.height` (number) - 截图高度（成功时）
- **平台**: ⚠️ 仅支持 Windows

**功能说明**：
- 调用后会创建全屏半透明黑色遮罩
- 鼠标变为十字光标
- 拖拽鼠标选择截图区域
- 释放鼠标后自动截图并保存到剪贴板
- 按 ESC 键可取消截图

**示例**:
```javascript
ScreenCapture.start((result) => {
  if (result.success) {
    console.log(`截图成功！尺寸: ${result.width}x${result.height}`);
    // 截图已在剪贴板中，可按 Ctrl+V 粘贴
  } else {
    console.log('截图已取消');
  }
});
```


## 🧪 测试

```bash
npm test
```

## ⚠️ 平台差异

| 特性 | macOS | Windows |
|-----|-------|---------|
| **窗口标识符** | Bundle ID (稳定，如 `com.apple.Safari`) | Process ID (动态变化，如 `12345`) |
| **激活限制** | 较宽松 | 严格（需要线程附加 hack） |
| **剪贴板监控** | 轮询 `changeCount` | 消息循环 + `WM_CLIPBOARDUPDATE` |
| **区域截图** | ❌ 暂不支持 | ✅ 支持（分层窗口 + GDI） |
| **权限要求** | 辅助功能权限（可选） | 无特殊要求 |

## 📝 注意事项

### macOS
- Bundle ID 是稳定的，应用重启后不变
- 推荐使用 Bundle ID 作为窗口标识

### Windows
- Process ID 每次启动都会变化，不适合持久化存储
- 激活窗口可能受到 Windows 安全限制
- 建议结合应用名称 (`appName`) 进行窗口识别

## 📄 License

MIT
