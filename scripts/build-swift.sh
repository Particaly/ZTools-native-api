#!/bin/bash

set -e

echo "🔨 Building Swift dynamic library..."

# 进入项目根目录
cd "$(dirname "$0")/.."

# 创建 lib 目录
mkdir -p lib

# 检测当前架构
ARCH=$(uname -m)
echo "📱 当前架构: $ARCH"

# 根据架构选择编译目标
if [ "$ARCH" = "arm64" ]; then
  TARGET="arm64-apple-macosx11.0"
else
  TARGET="x86_64-apple-macosx10.15"
fi

# 编译 Swift 为动态库
swiftc -emit-library \
  -o lib/libZToolsNative.dylib \
  src/ZToolsNative.swift \
  -framework Cocoa \
  -target $TARGET \
  -Osize

echo "✅ Swift library built successfully: lib/libZToolsNative.dylib"
