# 剪贴板文件操作功能

## 功能说明

该功能提供了完整的剪贴板文件操作接口：
- `getClipboardFiles()`: 获取剪贴板中的文件列表
- `setClipboardFiles()`: 设置文件到剪贴板

## 1. 获取剪贴板文件 (getClipboardFiles)

### 使用方法

```javascript
const { ClipboardMonitor } = require('ztools-native-api');

// 获取剪贴板中的文件列表
const files = ClipboardMonitor.getClipboardFiles();

console.log(files);
```

### 返回格式

返回一个数组，每个元素包含以下字段：

```javascript
[
  {
    path: 'D:\\ZTools-native-api',           // 文件完整路径
    name: 'ZTools-native-api',                // 文件名
    isDirectory: true                         // 是否是目录
  },
  {
    path: 'D:\\example.txt',
    name: 'example.txt',
    isDirectory: false
  }
]
```

### 字段说明

- `path` (string): 文件或文件夹的完整路径
- `name` (string): 文件或文件夹的名称
- `isDirectory` (boolean): 是否是目录
  - `true`: 文件夹
  - `false`: 文件

## 2. 设置剪贴板文件 (setClipboardFiles)

### 使用方法

```javascript
const { ClipboardMonitor } = require('ztools-native-api');

// 方式 1: 使用字符串数组（简洁）
ClipboardMonitor.setClipboardFiles([
  'C:\\test.txt',
  'C:\\folder',
  'D:\\document.pdf'
]);

// 方式 2: 使用对象数组（兼容 getClipboardFiles 返回值）
ClipboardMonitor.setClipboardFiles([
  { path: 'C:\\test.txt', name: 'test.txt', isDirectory: false },
  { path: 'C:\\folder', name: 'folder', isDirectory: true }
]);

// 方式 3: 直接使用 getClipboardFiles 的返回值
const files = ClipboardMonitor.getClipboardFiles();
// ... 处理文件列表 ...
ClipboardMonitor.setClipboardFiles(files);  // 写回剪贴板
```

### 参数说明

- `files` (Array): 文件路径数组
  - 支持字符串数组：`['path1', 'path2']`
  - 支持对象数组：`[{path: 'path1'}, {path: 'path2'}]`
  - 不能为空数组

### 返回值

- `boolean`: 操作是否成功
  - `true`: 设置成功
  - `false`: 设置失败

## 完整示例

### 示例 1: 读取并显示剪贴板文件

```javascript
const { ClipboardMonitor } = require('ztools-native-api');

try {
  const files = ClipboardMonitor.getClipboardFiles();

  if (files.length === 0) {
    console.log('剪贴板中没有文件');
  } else {
    console.log(`找到 ${files.length} 个文件/文件夹：`);

    files.forEach((file, index) => {
      const type = file.isDirectory ? '文件夹' : '文件';
      console.log(`${index + 1}. [${type}] ${file.name}`);
      console.log(`   路径: ${file.path}`);
    });
  }
} catch (error) {
  console.error('获取失败:', error.message);
}
```

### 示例 2: 设置指定文件到剪贴板

```javascript
const { ClipboardMonitor } = require('ztools-native-api');

try {
  const filesToCopy = [
    'C:\\Users\\Public\\Documents\\report.pdf',
    'C:\\Users\\Public\\Pictures\\photo.jpg',
    'C:\\Projects\\my-app'
  ];

  const success = ClipboardMonitor.setClipboardFiles(filesToCopy);

  if (success) {
    console.log('✅ 文件已复制到剪贴板');
    console.log('现在可以在文件资源管理器中粘贴（Ctrl+V）');
  } else {
    console.log('❌ 复制失败');
  }
} catch (error) {
  console.error('设置失败:', error.message);
}
```

### 示例 3: 读取、过滤、再写入

```javascript
const { ClipboardMonitor } = require('ztools-native-api');
const path = require('path');

try {
  // 读取剪贴板文件
  const files = ClipboardMonitor.getClipboardFiles();

  // 过滤：只保留 .txt 文件
  const txtFiles = files.filter(file => {
    return !file.isDirectory && path.extname(file.path) === '.txt';
  });

  if (txtFiles.length > 0) {
    // 将过滤后的文件写回剪贴板
    const success = ClipboardMonitor.setClipboardFiles(txtFiles);
    console.log(`已过滤，剩余 ${txtFiles.length} 个 .txt 文件`);
  } else {
    console.log('没有找到 .txt 文件');
  }
} catch (error) {
  console.error('操作失败:', error.message);
}
```

## 注意事项

1. **平台支持**：目前仅支持 Windows 平台
   - macOS 平台调用这些方法会抛出错误

2. **文件路径有效性**：`setClipboardFiles` 不会验证文件是否存在
   - 可以设置不存在的文件路径到剪贴板
   - 但粘贴时可能会失败

3. **空剪贴板**：
   - `getClipboardFiles()` 在没有文件时返回空数组 `[]`
   - `setClipboardFiles([])` 会抛出错误（不能为空数组）

4. **使用前提**：
   - **获取**：需要先在文件资源管理器中复制文件（`Ctrl+C`）
   - **设置**：设置后可以在任何地方粘贴（`Ctrl+V`）

5. **剪贴板清空**：`setClipboardFiles` 会清空剪贴板的原有内容

## 测试

项目提供了多个测试文件：

### 1. 获取剪贴板文件（简单测试）
```bash
node test/test-clipboard-files-simple.js
```
直接显示当前剪贴板中的文件

### 2. 获取剪贴板文件（交互式测试）
```bash
node test/test-clipboard-files.js
```
按照提示操作，逐步测试功能

### 3. 设置剪贴板文件（完整测试）
```bash
node test/test-clipboard-files-set.js
```
测试读取、写入和验证完整流程

## API 总结

| 方法 | 参数 | 返回值 | 说明 |
|------|------|--------|------|
| `getClipboardFiles()` | 无 | `Array<{path, name, isDirectory}>` | 获取剪贴板文件列表 |
| `setClipboardFiles(files)` | `Array<string\|object>` | `boolean` | 设置文件到剪贴板 |
