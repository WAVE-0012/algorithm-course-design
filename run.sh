#!/bin/bash
cd "$(dirname "$0")" || exit 1

if [ -f "./build/gpu_scheduler" ]; then
    ./build/gpu_scheduler
elif [ -f "./build/Debug/gpu_scheduler" ]; then
    ./build/Debug/gpu_scheduler
else
    echo "找不到可执行文件"
    exit 1
fi
