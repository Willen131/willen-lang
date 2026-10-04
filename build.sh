#!/bin/bash
# Willen 语言解释器构建脚本
set -e

cd "$(dirname "$0")"

# 固定使用项目内的临时目录，不用系统 TEMP。
#
# 原因：系统临时目录（%TEMP%）里文件堆积到数千个之后，g++ 的编译器前端
# cc1plus 会无法在其中创建临时文件，然后**不给任何错误信息就退出**——
# 表现为构建失败但屏幕上什么都没有，极难排查。用项目内的空目录可彻底避免。
BUILD_TMP="$PWD/.build-tmp"
mkdir -p "$BUILD_TMP"
export TMP="$BUILD_TMP"
export TEMP="$BUILD_TMP"
export TMPDIR="$BUILD_TMP"

# -static 必须保留，否则 willen.exe 依赖 MinGW 运行库 DLL，换机器无法运行
g++ -std=c++17 -O2 -static -o willen.exe src/*.cpp
echo "构建完成：willen.exe"
