const { ColorPicker } = require('../index');

console.log('=== 取色器测试 ===');
console.log('移动鼠标查看放大镜效果');
console.log('点击鼠标左键选取颜色');
console.log('按 ESC 键取消');
console.log('---');

ColorPicker.start((result) => {
  if (result.success) {
    console.log(`选中颜色: ${result.hex}`);
  } else {
    console.log('取色已取消');
  }
  process.exit(0);
});

// 30 秒超时自动停止
setTimeout(() => {
  console.log('超时，自动停止');
  ColorPicker.stop();
  process.exit(0);
}, 30000);
