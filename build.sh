#!/bin/bash
# Willen 语言解释器构建脚本
# -static 必须保留，否则 willen.exe 依赖 MinGW 运行库 DLL，换机器无法运行
set -e
g++ -std=c++17 -O2 -static -o willen.exe src/*.cpp
echo "构建完成：willen.exe"
