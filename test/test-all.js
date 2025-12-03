const { ClipboardMonitor, WindowMonitor, WindowManager } = require('../index');
const os = require('os');

const platform = os.platform();
console.log(`\n${'='.repeat(60)}`);
console.log(`  ZTools Native API 功能测试 (${platform})`);
console.log('='.repeat(60));
console.log('');

// 事件计数器
let clipboardEvents = 0;
let windowEvents = 0;

// 测试1: 获取当前窗口
console.log('【测试 1】获取当前激活窗口');
const activeWindow = WindowManager.getActiveWindow();
if (activeWindow) {
  console.log(`  ✅ 应用名: ${activeWindow.appName}`);
  if (platform === 'darwin') {
    console.log(`     Bundle ID: ${activeWindow.bundleId}`);
  } else {
    console.log(`     进程ID: ${activeWindow.processId}`);
  }
} else {
  console.log('  ❌ 获取失败');
}
console.log('');

// 测试2: 窗口激活监控
console.log('【测试 2】窗口激活监控');
console.log('  提示: 切换到其他应用再切回来');
console.log('');

const windowMonitor = new WindowMonitor();
windowMonitor.start((windowInfo) => {
  windowEvents++;
  const time = new Date().toLocaleTimeString();
  if (platform === 'darwin') {
    console.log(`  [${time}] ${windowInfo.appName} (${windowInfo.bundleId})`);
  } else {
    console.log(`  [${time}] ${windowInfo.appName} (PID: ${windowInfo.processId})`);
  }
});

// 测试3: 剪贴板监控
console.log('【测试 3】剪贴板变化监控');
console.log('  提示: 复制一些文本测试');
console.log('');

const clipboardMonitor = new ClipboardMonitor();
clipboardMonitor.start(() => {
  clipboardEvents++;
  const time = new Date().toLocaleTimeString();
  console.log(`  [${time}] 剪贴板变化 #${clipboardEvents}`);
});

// 倒计时显示
let remaining = 30;
const countdown = setInterval(() => {
  process.stdout.write(`\r  剩余时间: ${remaining} 秒...`);
  remaining--;
  if (remaining < 0) {
    clearInterval(countdown);
  }
}, 1000);

// 30秒后停止所有监控
setTimeout(() => {
  clearInterval(countdown);
  process.stdout.write('\r');
  console.log('');
  console.log('='.repeat(60));
  console.log('【测试结果】');
  console.log(`  窗口切换事件: ${windowEvents} 个`);
  console.log(`  剪贴板变化: ${clipboardEvents} 个`);
  console.log('='.repeat(60));
  console.log('');
  console.log('✅ 所有测试完成，监控已停止');
  console.log('');

  windowMonitor.stop();
  clipboardMonitor.stop();
  process.exit(0);
}, 30000);

// 处理 Ctrl+C
process.on('SIGINT', () => {
  clearInterval(countdown);
  console.log('\n\n用户中断测试');
  console.log(`  窗口切换事件: ${windowEvents} 个`);
  console.log(`  剪贴板变化: ${clipboardEvents} 个`);
  windowMonitor.stop();
  clipboardMonitor.stop();
  process.exit(0);
});
