#!/usr/bin/env node
const fs = require('fs');
const path = require('path');
const os = require('os');

const platform = os.platform();

console.log(`🧹 Cleaning build artifacts for ${platform}...\n`);

// 删除 build 目录
const buildDir = path.join(__dirname, '..', 'build');
if (fs.existsSync(buildDir)) {
  console.log('🗑️  Removing build/');
  fs.rmSync(buildDir, { recursive: true, force: true });
}

// macOS: 删除 Swift dylib
if (platform === 'darwin') {
  const libDir = path.join(__dirname, '..', 'lib');
  if (fs.existsSync(libDir)) {
    const dylibFiles = fs.readdirSync(libDir).filter(f => f.endsWith('.dylib'));
    dylibFiles.forEach(file => {
      console.log(`🗑️  Removing lib/${file}`);
      fs.unlinkSync(path.join(libDir, file));
    });
  }
}

// Windows: 删除可能的临时文件
if (platform === 'win32') {
  const tempFiles = ['.obj', '.pdb', '.exp', '.lib'];
  const rootDir = path.join(__dirname, '..');

  tempFiles.forEach(ext => {
    const files = fs.readdirSync(rootDir).filter(f => f.endsWith(ext));
    files.forEach(file => {
      console.log(`🗑️  Removing ${file}`);
      fs.unlinkSync(path.join(rootDir, file));
    });
  });
}

console.log('\n✅ Clean complete!');
