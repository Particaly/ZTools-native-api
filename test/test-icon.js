const { IconExtractor } = require('../index');
const fs = require('fs');
const path = require('path');

console.log('\n' + '='.repeat(60));
console.log('  IconExtractor 测试');
console.log('='.repeat(60) + '\n');

const lnkPath = 'C:\\Users\\Admin\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\System Tools\\File Explorer.lnk';
const outputDir = path.join(__dirname, 'output');

// 创建输出目录
if (!fs.existsSync(outputDir)) {
  fs.mkdirSync(outputDir, { recursive: true });
}

// 检查 lnk 文件是否存在
if (!fs.existsSync(lnkPath)) {
  console.log('  ⚠ 指定的 lnk 文件不存在:', lnkPath);
  console.log('  尝试使用 explorer.exe 替代...\n');

  const explorerPath = path.join(process.env.WINDIR || 'C:\\Windows', 'explorer.exe');
  testIcon(explorerPath);
} else {
  testIcon(lnkPath);
}

function testIcon(filePath) {
  console.log('  目标文件:', filePath);
  console.log('');

  const sizes = [16, 32, 64, 256];

  for (const size of sizes) {
    const icon = IconExtractor.getFileIcon(filePath, size);
    const fileName = `icon_${size}x${size}.png`;
    const outputPath = path.join(outputDir, fileName);

    if (icon) {
      fs.writeFileSync(outputPath, icon);
      console.log(`  ✅ ${size}x${size}: ${icon.length} bytes -> ${outputPath}`);
    } else {
      console.log(`  ❌ ${size}x${size}: 获取失败`);
    }
  }

  console.log('\n' + '='.repeat(60));
  console.log('  测试完成，PNG 文件已保存到:', outputDir);
  console.log('='.repeat(60) + '\n');
}
