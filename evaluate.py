#!/usr/bin/env python3
"""
评估脚本：计算 GPU 调度器的三个评估指标
使用方法：python3 evaluate.py case001_eval.out testcases/case001.in
"""

import sys
import math

def read_instance(in_file):
    """读取 .in 文件，返回服务器列表和任务列表"""
    with open(in_file, 'r') as f:
        lines = f.readlines()
    
    # 第一行：M N
    M, N = map(int, lines[0].split())
    
    # 服务器配置
    servers = []
    for i in range(1, 1 + M):
        G, VG, C, R = map(int, lines[i].split())
        servers.append({
            'id': i,
            'gpu_count': G,
            'gpu_memory': VG,
            'cpu': C,
            'memory': R
        })
    
    # 任务配置
    jobs = []
    for i in range(1 + M, 1 + M + N):
        r, p, g, v, c, m, w = map(int, lines[i].split())
        jobs.append({
            'id': i - M,
            'release_time': r,
            'duration': p,
            'min_gpu': g,
            'gpu_memory': v,
            'cpu': c,
            'memory': m,
            'weight': w
        })
    
    return servers, jobs

def read_schedule(out_file):
    """读取 .out 文件，返回调度结果列表"""
    schedules = []
    with open(out_file, 'r') as f:
        for line in f:
            parts = line.strip().split()
            if not parts:
                continue
            job_id, server_id, start_time, gpu_used, finish_time = map(int, parts)
            schedules.append({
                'job_id': job_id,
                'server_id': server_id,
                'start_time': start_time,
                'gpu_used': gpu_used,
                'finish_time': finish_time
            })
    return schedules

def compute_metrics(servers, jobs, schedules):
    """计算三个评估指标"""
    # 构建 job_id -> schedule 的映射
    schedule_map = {s['job_id']: s for s in schedules}
    
    N = len(jobs)
    
    # 1. E_wait：任务等待评估
    E_wait = 0
    for job in jobs:
        s = schedule_map.get(job['id'])
        if s:
            E_wait += job['weight'] * (s['start_time'] - job['release_time'])
    
    # 2. E_memory：GPU显存空闲评估（修正版）
    # 计算总显存需求权重 p_i (均匀权重 1/N)
    E_memory = 0
    total_duration = sum(job['duration'] for job in jobs)
    for job in jobs:
        s = schedule_map.get(job['id'])
        if s:
            # 找到对应的服务器
            server = None
            for sv in servers:
                if sv['id'] == s['server_id']:
                    server = sv
                    break
            if server:
                # p_i = duration / total_duration (任务持续时长占总时长的比例)
                p_i = job['duration'] / total_duration if total_duration > 0 else 1.0 / N
                allocated_memory = s['gpu_used'] * server['gpu_memory']
                E_memory += p_i * (allocated_memory - job['gpu_memory'])
    
    # 3. E_finish：任务完成评估
    H = 0
    for s in schedules:
        if s['finish_time'] > H:
            H = s['finish_time']
    E_finish = H
    
    return E_wait, E_memory, E_finish, N

def main():
    if len(sys.argv) < 3:
        print("用法: python3 evaluate.py <out_file> <in_file>")
        sys.exit(1)
    
    out_file = sys.argv[1]
    in_file = sys.argv[2]
    
    # 读取数据
    servers, jobs = read_instance(in_file)
    schedules = read_schedule(out_file)
    
    # 计算指标
    E_wait, E_memory, E_finish, N = compute_metrics(servers, jobs, schedules)
    
    print(f"任务数: {N}")
    print(f"E_wait (任务等待评估): {E_wait:.2f}")
    print(f"E_memory (GPU显存空闲评估): {E_memory:.2f}")
    print(f"E_finish (任务完成评估): {E_finish}")
    print(f"\n单实例原始指标 (越小越好):")
    print(f"  E_wait: {E_wait:.2f}")
    print(f"  E_memory: {E_memory:.2f}")
    print(f"  E_finish: {E_finish}")

if __name__ == "__main__":
    main()