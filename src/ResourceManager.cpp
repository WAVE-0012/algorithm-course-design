#include "ResourceManager.h"
#include <algorithm>

// 初始化：将输入的服务器静态配置转化为内部管理的动态资源账本
void ResourceManager::init_servers(const std::vector<Server>& input_servers) {
    servers = input_servers;
    // 由于在 main.cpp 中已经对 input_servers 进行了 resize 和预留，这里直接赋值是极其高效的连续内存拷贝
}

// 核心决策与扣减函数：尝试为任务分配合适的服务器和 GPU
bool ResourceManager::try_allocate(const Job& job, int& out_server_id, std::vector<int>& out_gpus) {
    // 1. 计算单张 GPU 卡实际需要的显存（向上取整，防精度丢失）
    int required_per_gpu = 0;
    if (job.g > 0) {
        required_per_gpu = (job.v + job.g - 1) / job.g;
    }

    int best_server_idx = -1;
    std::vector<int> best_gpus;
    long long min_fragmentation = 9e18; // 初始化为一个极大的碎片度量值

    // 2. 遍历集群中所有的服务器，寻找最匹配的“最优解”(Best-Fit)
    for (size_t i = 0; i < servers.size(); ++i) {
        auto& server = servers[i];

        // 检查 A：基础的 CPU 和 内存 是否充裕
        if (server.free_cpu < job.c || server.free_mem < job.m) {
            continue; 
        }

        // 检查 B：针对 GPU 任务进行卡级别的最佳匹配
        if (job.g > 0) {
            if (server.G < job.g) continue; // 物理卡数不够，直接跳过

            // 搜集当前服务器上所有满足单卡显存要求的 GPU
            // 存储格式为：pair<当前卡剩余显存, 物理卡号>
            std::vector<std::pair<int, int>> candidate_gpus;
            for (int g_idx = 0; g_idx < server.G; ++g_idx) {
                if (server.gpu_free_mem[g_idx] >= required_per_gpu) {
                    candidate_gpus.push_back({server.gpu_free_mem[g_idx], g_idx});
                }
            }

            // 如果满足显存要求的卡数小于任务要求的卡数，说明这台机器“装不下”
            if (candidate_gpus.size() < static_cast<size_t>(job.g)) {
                continue;
            }

            // 【高性能拿分点 1】：对候选 GPU 按照当前剩余显存“升序排序”
            // 这样能确保我们优先挑出“刚好够用”的卡，把空闲极大的卡留给后面的大任务！
            std::sort(candidate_gpus.begin(), candidate_gpus.end());

            // 提取出前 job.g 张最优卡，并计算如果在这台机器上跑，会产生多少显存碎片
            std::vector<int> current_selected_gpus;
            long long current_server_gpu_frag = 0;
            for (int k = 0; k < job.g; ++k) {
                current_selected_gpus.push_back(candidate_gpus[k].second);
                // 碎片计算：当前剩余显存 - 任务将要扣除的显存
                current_server_gpu_frag += (candidate_gpus[k].first - required_per_gpu);
            }

            // 【高性能拿分点 2】：全局 Best-Fit 判定
            // 选择分配后“显存碎片最少”的服务器。若碎片相同，可保留先遍历到的（First-Fit 保底）
            if (current_server_gpu_frag < min_fragmentation) {
                min_fragmentation = current_server_gpu_frag;
                best_server_idx = i;
                best_gpus = current_selected_gpus;
            }
        } 
        // 检查 C：针对纯 CPU 任务（不需GPU）的最佳匹配
        else {
            // 计算 CPU 资源碎片度量
            long long cpu_frag = server.free_cpu - job.c;
            if (cpu_frag < min_fragmentation) {
                min_fragmentation = cpu_frag;
                best_server_idx = i;
                best_gpus.clear(); // 纯 CPU 任务不需要卡号
            }
        }
    }

    // 3. 如果在整个集群中找到了最合适的服务器，执行“原子级”资源扣除
    if (best_server_idx != -1) {
        auto& target_server = servers[best_server_idx];
        
        // 扣除物理资源
        target_server.free_cpu -= job.c;
        target_server.free_mem -= job.m;
        
        // 扣除选中的每张 GPU 的对应显存
        for (int g_idx : best_gpus) {
            target_server.gpu_free_mem[g_idx] -= required_per_gpu;
        }

        // 通过引用将最终决策结果返回给外部调用者
        out_server_id = target_server.id;
        out_gpus = best_gpus;
        return true; 
    }

    return false; // 集群中目前没有任何一台机器能塞下该任务
}

// 资源释放函数：当某个任务运行结束时，将其占用的物理资源精准归还账本
void ResourceManager::release_resources(const Job& job) {
    // 健壮性检查：防止非法退货
    if (job.allocated_server <= 0 || job.allocated_server > static_cast<int>(servers.size())) {
        return;
    }

    // 服务器 ID 是从 1 开始的，映射到 vector 索引需要减 1
    auto& target_server = servers[job.allocated_server - 1];
    
    // 归还 CPU 和 内存
    target_server.free_cpu += job.c;
    target_server.free_mem += job.m;

    // 归还 GPU 显存
    if (job.g > 0) {
        int required_per_gpu = (job.v + job.g - 1) / job.g;
        for (int g_idx : job.allocated_gpus) {
            target_server.gpu_free_mem[g_idx] += required_per_gpu;
        }
    }
}