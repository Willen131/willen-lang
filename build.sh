#!/bin/bash
# Willen 语言解释器构建脚本
set -e

cd "$(dirname "$0")"

# 临时目录：固定用 $USERPROFILE 下的一个专用目录。
#
# 这个位置有两个必须满足的条件，缺一不可：
#
# ① 不能是系统 %TEMP%。
#    系统临时目录里文件堆积到数千个之后，g++ 的编译器前端 cc1plus 会无法
#    在其中创建临时文件，然后**不给任何错误信息就退出**——表现为构建失败
#    但屏幕上什么都没有，极难排查（Day 7 遇到过）。
#
# ② 路径不能含中文。
#    MinGW 的 g++ 通过 ANSI 接口获取工作目录，中文路径在编码转换中会变成
#    乱码，汇编器随即报「can't create ...: No such file or directory」。
#    更隐蔽的是：**这个问题只在特定调用方式下出现**——从交互式 Git Bash
#    运行 build.sh 时 PWD 是正确的 UTF-8，一切正常；而从 Python 的
#    subprocess 调用时，环境里继承的 PWD 编码已被破坏，构建就失败了。
#    $USERPROFILE（C:\Users\willen）是纯 ASCII，两种方式下都安全。
#
# 每次构建前清空该目录，避免它自己也堆积起来。
BUILD_TMP="$(cygpath -m "$USERPROFILE" 2>/dev/null || echo "$USERPROFILE")/willen-build-tmp"
rm -rf "$BUILD_TMP"
mkdir -p "$BUILD_TMP"
export TMP="$BUILD_TMP"
export TEMP="$BUILD_TMP"
export TMPDIR="$BUILD_TMP"

# -static 必须保留，否则 willen.exe 依赖 MinGW 运行库 DLL，换机器无法运行
g++ -std=c++17 -O2 -static -o willen.exe src/*.cpp
echo "构建完成：willen.exe"
