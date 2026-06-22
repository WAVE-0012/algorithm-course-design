#!/bin/bash
# 创建一个独立的 build 文件夹，避免编译产生的缓存垃圾污染源码
mkdir -p build && cd build

# 让 CMake 读取上一级目录的 CMakeLists.txt，并生成 Makefile
cmake -DCMAKE_BUILD_TYPE=Release ..

# 开始真正的编译
cmake --build .