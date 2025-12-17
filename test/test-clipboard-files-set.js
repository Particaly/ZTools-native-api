const { ClipboardMonitor } = require('../index');

console.log('\n========================================');
console.log('  设置剪贴板文件测试');
console.log('========================================\n');

try {
  // 测试 1: 读取当前剪贴板文件
  console.log('【测试 1】读取当前剪贴板文件');
  console.log('─'.repeat(60));

  const originalFiles = ClipboardMonitor.getClipboardFiles();

  if (!originalFiles || originalFiles.length === 0) {
    console.log('剪贴板中没有文件，使用测试文件路径...\n');

    // 使用当前目录的一些文件作为测试
    const testFiles = [
      'D:\\ZTools-native-api\\package.json',
      'D:\\ZTools-native-api\\README.md',
      'D:\\ZTools-native-api\\test'
    ];

    console.log('【测试 2】设置测试文件到剪贴板');
    console.log('─'.repeat(60));
    console.log('写入以下文件到剪贴板：');
    testFiles.forEach((path, index) => {
      console.log(`  ${index + 1}. ${path}`);
    });

    const success = ClipboardMonitor.setClipboardFiles(testFiles);

    if (success) {
      console.log('\n✅ 设置成功！\n');

      // 验证读取
      console.log('【测试 3】验证写入结果');
      console.log('─'.repeat(60));
      const readBack = ClipboardMonitor.getClipboardFiles();
      console.log(`读取到 ${readBack.length} 个文件：\n`);

      readBack.forEach((file, index) => {
        const type = file.isDirectory ? '📁 文件夹' : '📄 文件';
        console.log(`${index + 1}. ${type}: ${file.name}`);
        console.log(`   路径: ${file.path}`);
      });

      console.log('\n✅ 读写测试通过！');
    } else {
      console.log('\n❌ 设置失败');
    }

  } else {
    console.log(`读取到 ${originalFiles.length} 个文件：\n`);

    originalFiles.forEach((file, index) => {
      const type = file.isDirectory ? '📁 文件夹' : '📄 文件';
      console.log(`${index + 1}. ${type}: ${file.name}`);
      console.log(`   路径: ${file.path}`);
    });

    console.log('\n【测试 2】将文件写回剪贴板（测试循环）');
    console.log('─'.repeat(60));

    // 测试使用对象数组格式（从 getClipboardFiles 返回的格式）
    const success1 = ClipboardMonitor.setClipboardFiles(originalFiles);
    console.log(`使用对象数组格式: ${success1 ? '✅ 成功' : '❌ 失败'}`);

    // 测试使用字符串数组格式
    const pathStrings = originalFiles.map(f => f.path);
    const success2 = ClipboardMonitor.setClipboardFiles(pathStrings);
    console.log(`使用字符串数组格式: ${success2 ? '✅ 成功' : '❌ 失败'}`);

    // 验证读取
    console.log('\n【测试 3】验证写入结果');
    console.log('─'.repeat(60));
    const readBack = ClipboardMonitor.getClipboardFiles();
    console.log(`读取到 ${readBack.length} 个文件：\n`);

    readBack.forEach((file, index) => {
      const type = file.isDirectory ? '📁 文件夹' : '📄 文件';
      console.log(`${index + 1}. ${type}: ${file.name}`);
      console.log(`   路径: ${file.path}`);
    });

    // 比较结果
    const pathsMatch = readBack.length === originalFiles.length &&
      readBack.every((file, i) => file.path === originalFiles[i].path);

    if (pathsMatch) {
      console.log('\n✅ 所有测试通过！读写内容一致');
    } else {
      console.log('\n⚠️  警告：读写内容不完全一致');
    }
  }

  console.log('\n【JSON 格式示例】');
  console.log('─'.repeat(60));
  console.log('// 方式 1: 使用对象数组（兼容 getClipboardFiles 返回值）');
  console.log('ClipboardMonitor.setClipboardFiles([');
  console.log('  { path: "C:\\\\file1.txt", name: "file1.txt", isDirectory: false },');
  console.log('  { path: "C:\\\\folder", name: "folder", isDirectory: true }');
  console.log(']);\n');

  console.log('// 方式 2: 使用字符串数组（简洁）');
  console.log('ClipboardMonitor.setClipboardFiles([');
  console.log('  "C:\\\\file1.txt",');
  console.log('  "C:\\\\folder"');
  console.log(']);');

  console.log('\n========================================\n');

} catch (error) {
  console.error('❌ 错误:', error.message);
  console.error(error.stack);
  process.exit(1);
}
