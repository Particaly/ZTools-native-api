const { ClipboardMonitor } = require('../index');
const os = require('os');

const platform = os.platform();
console.log(`\n${'='.repeat(60)}`);
console.log(`  ZTools Native API - 获取剪贴板文件测试 (${platform})`);
console.log('='.repeat(60));
console.log('');

// 主测试函数
function runTest() {
  try {
    console.log('【步骤 1】准备测试');
    console.log('  请按照以下步骤操作：');
    console.log('  1. 打开文件资源管理器（Windows）');
    console.log('  2. 选择一个或多个文件/文件夹');
    console.log('  3. 按 Ctrl+C 复制这些文件');
    console.log('  4. 回到终端，按回车键继续');
    console.log('');

    // 等待用户按回车
    process.stdin.once('data', () => {
      console.log('【步骤 2】获取剪贴板文件列表');

      try {
        const files = ClipboardMonitor.getClipboardFiles();

        if (!files || files.length === 0) {
          console.log('  ❌ 剪贴板中没有文件');
          console.log('  提示: 请确保已复制文件到剪贴板');
        } else {
          console.log(`  ✅ 成功获取 ${files.length} 个文件/文件夹`);
          console.log('');
          console.log('【文件列表】');

          files.forEach((file, index) => {
            console.log(`  ${index + 1}. ${file.name}`);
            console.log(`     路径: ${file.path}`);
            console.log(`     类型: ${file.isDirectory ? '文件夹' : '文件'}`);
            console.log('');
          });

          console.log('【JSON格式】');
          console.log(JSON.stringify(files, null, 2));
        }

      } catch (error) {
        console.error('  ❌ 获取失败:', error.message);
      }

      console.log('');
      console.log('='.repeat(60));
      console.log('');
      process.exit(0);
    });

  } catch (error) {
    console.error('❌ 测试过程中出错:', error.message);
    process.exit(1);
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
