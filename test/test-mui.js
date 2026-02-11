const { MuiResolver } = require('../index');

console.log('\n' + '='.repeat(60));
console.log('  MuiResolver 测试');
console.log('='.repeat(60) + '\n');

const refs = [
  '@%SystemRoot%\\system32\\shell32.dll,-22067',
  '@%SystemRoot%\\system32\\shell32.dll,-21769',
  '@%SystemRoot%\\system32\\shell32.dll,-21782',
  '@%SystemRoot%\\system32\\notepad.exe,-469',
  '@%SystemRoot%\\system32\\control.exe,-1',
  '@%SystemRoot%\\system32\\mmsys.cpl,-1',
];

console.log('  输入 MUI 引用:');
refs.forEach((r, i) => console.log(`    [${i}] ${r}`));
console.log('');

console.time('  耗时');
const result = MuiResolver.resolve(refs);
console.timeEnd('  耗时');

console.log('');
console.log('  解析结果:');
for (const [ref, name] of Object.entries(result)) {
  console.log(`    ${ref}`);
  console.log(`      -> ${name}`);
}

const resolved = Object.keys(result).length;
const total = refs.length;
console.log(`\n  成功: ${resolved}/${total}`);
console.log('\n' + '='.repeat(60) + '\n');
