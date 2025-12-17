const { ClipboardMonitor } = require('../index');

console.log('\n========================================');
console.log('  获取剪贴板文件测试');
console.log('========================================\n');

try {
  console.log('正在获取剪贴板文件...\n');

  const files = ClipboardMonitor.getClipboardFiles();

  if (!files || files.length === 0) {
    console.log('剪贴板中没有文件');
    console.log('\n提示：');
    console.log('1. 在文件资源管理器中选择文件或文件夹');
    console.log('2. 按 Ctrl+C 复制');
    console.log('3. 重新运行此测试\n');
  } else {
    console.log(`✅ 成功获取 ${files.length} 个文件/文件夹\n`);
    console.log('文件列表：');
    console.log('─'.repeat(60));

    files.forEach((file, index) => {
      const type = file.isDirectory ? '📁 文件夹' : '📄 文件';
      console.log(`\n${index + 1}. ${type}: ${file.name}`);
      console.log(`   路径: ${file.path}`);
    });

    console.log('\n' + '─'.repeat(60));
    console.log('\nJSON 格式：');
    console.log(JSON.stringify(files, null, 2));
  }

  console.log('\n========================================\n');

} catch (error) {
  console.error('❌ 错误:', error.message);
  console.error(error.stack);
  process.exit(1);
}
