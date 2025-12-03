const { WindowManager } = require('../index');
const os = require('os');
const { exec } = require('child_process');

const platform = os.platform();
console.log(`\n${'='.repeat(60)}`);
console.log(`  ZTools Native API - 模拟粘贴功能测试 (${platform})`);
console.log('='.repeat(60));
console.log('');

// 设置剪贴板内容（用于测试）
function setClipboard(text) {
  return new Promise((resolve, reject) => {
    let command;
    if (platform === 'darwin') {
      command = `echo "${text}" | pbcopy`;
    } else if (platform === 'win32') {
      command = `echo ${text} | clip`;
    } else {
      reject(new Error('Unsupported platform'));
      return;
    }

    exec(command, (error) => {
      if (error) {
        reject(error);
      } else {
        resolve();
      }
    });
  });
}

// 主测试函数
async function runTest() {
  try {
    // 步骤1: 设置测试文本到剪贴板
    const testText = '测试粘贴功能 - Test Paste Function - ' + new Date().toISOString();
    console.log('【步骤 1】设置剪贴板内容');
    console.log(`  内容: "${testText}"`);
    await setClipboard(testText);
    console.log('  ✅ 剪贴板已设置');
    console.log('');

    // 步骤2: 提示用户切换到文本编辑器
    console.log('【步骤 2】准备测试');
    console.log('  请按照以下步骤操作：');
    console.log('  1. 打开一个文本编辑器（记事本、TextEdit等）');
    console.log('  2. 在编辑器中点击一个位置（获得焦点）');
    console.log('  3. 回到终端，按回车键继续');
    console.log('');

    // 等待用户按回车
    await new Promise((resolve) => {
      process.stdin.once('data', resolve);
    });

    console.log('【步骤 3】执行模拟粘贴');
    console.log(`  操作: 模拟按键 ${platform === 'darwin' ? 'Command+V' : 'Ctrl+V'}`);

    // 延迟3秒，给用户时间切换到编辑器
    console.log('  倒计时: 3秒后自动执行...');
    await new Promise(resolve => setTimeout(resolve, 1000));
    console.log('  倒计时: 2秒...');
    await new Promise(resolve => setTimeout(resolve, 1000));
    console.log('  倒计时: 1秒...');
    await new Promise(resolve => setTimeout(resolve, 1000));

    // 步骤3: 调用模拟粘贴
    const success = WindowManager.simulatePaste();
    console.log(`  结果: ${success ? '✅ 成功' : '❌ 失败'}`);
    console.log('');

    if (success) {
      console.log('【测试结果】');
      console.log('  ✅ 模拟粘贴功能正常');
      if (platform === 'darwin') {
        console.log('  注意: 如果没有粘贴成功，请检查：');
        console.log('  - 是否授予了辅助功能权限');
        console.log('  - 系统偏好设置 > 安全性与隐私 > 辅助功能');
      }
    } else {
      console.log('【测试结果】');
      console.log('  ❌ 模拟粘贴失败');
      if (platform === 'darwin') {
        console.log('  可能原因：');
        console.log('  - 未授予辅助功能权限');
        console.log('  - 请到：系统偏好设置 > 安全性与隐私 > 辅助功能');
        console.log('  - 添加当前应用（Terminal或Node）到允许列表');
      }
    }

    console.log('');
    console.log('='.repeat(60));
    console.log('');

  } catch (error) {
    console.error('❌ 测试过程中出错:', error.message);
  } finally {
    process.exit(0);
  }
}

// 启动测试
console.log('提示: 按 Ctrl+C 可以随时退出测试');
console.log('');

// 处理 Ctrl+C
process.on('SIGINT', () => {
  console.log('\n\n用户中断测试');
  process.exit(0);
});

// 延迟启动，让用户看到提示
setTimeout(() => {
  runTest();
}, 500);
