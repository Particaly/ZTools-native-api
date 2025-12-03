# 模拟粘贴功能使用说明

## 功能描述

`WindowManager.simulatePaste()` 方法可以模拟系统级别的粘贴操作：
- **Mac**: 模拟 `Command + V`
- **Windows**: 模拟 `Ctrl + V`

## API

```javascript
const { WindowManager } = require('ztools-native-api');

// 模拟粘贴操作
const success = WindowManager.simulatePaste();

if (success) {
  console.log('粘贴命令已发送');
} else {
  console.log('粘贴失败');
}
```

## 权限要求

### macOS
在Mac上使用此功能需要**辅助功能权限**（Accessibility Permission）：

1. 打开 **系统偏好设置** > **安全性与隐私** > **辅助功能**
2. 点击左下角的锁图标解锁
3. 将你的应用（Terminal、Node、或你的应用程序）添加到允许列表
4. 首次调用时会自动弹出权限请求对话框

### Windows
Windows 平台通常不需要特殊权限。

## 使用场景

```javascript
const { WindowManager } = require('ztools-native-api');
const { exec } = require('child_process');

// 场景1: 设置剪贴板内容后自动粘贴
async function copyAndPaste(text) {
  // 1. 设置剪贴板（Mac示例）
  await new Promise((resolve, reject) => {
    exec(`echo "${text}" | pbcopy`, (error) => {
      if (error) reject(error);
      else resolve();
    });
  });

  // 2. 等待一小段时间
  await new Promise(resolve => setTimeout(resolve, 100));

  // 3. 模拟粘贴
  WindowManager.simulatePaste();
}

// 使用
copyAndPaste('Hello World');
```

## 注意事项

1. **焦点窗口**: 粘贴操作会发送到当前具有焦点的窗口
2. **剪贴板内容**: 此方法只是模拟按键，实际粘贴的内容取决于系统剪贴板
3. **延迟**: 建议在设置剪贴板和调用粘贴之间留100-200ms的延迟
4. **权限检查**: Mac上如果返回false，通常表示没有辅助功能权限

## 测试

运行测试脚本：

```bash
# 简单测试（自动运行）
node test/test-paste-simple.js

# 完整测试（需要手动操作）
node test/test-paste.js
```

## 实现原理

### macOS
使用 Core Graphics 的 `CGEvent` API：
1. 创建 `CGEventSource`
2. 创建按键事件（V键）
3. 添加 Command 修饰符（`CGEventFlags.maskCommand`）
4. 通过 `CGEventPost` 发送事件

### Windows
使用 Windows API 的 `SendInput` 函数：
1. 创建 INPUT 数组（4个事件）
2. 按下 Ctrl 键
3. 按下 V 键
4. 释放 V 键
5. 释放 Ctrl 键

## 故障排除

### Mac: 返回false或没有效果
- 检查是否授予了辅助功能权限
- 尝试重启终端或应用
- 检查系统偏好设置中是否正确添加了应用

### Windows: 没有粘贴效果
- 确保目标窗口有焦点
- 确认剪贴板有内容
- 检查目标应用是否支持 Ctrl+V

## 兼容性

- **macOS**: 10.9+ (需要辅助功能权限)
- **Windows**: Windows 7+
- **Node.js**: 16.0.0+
