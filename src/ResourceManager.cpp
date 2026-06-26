#include "ResourceManager.h"
#include <algorithm>

void ResourceManager::init_servers(const std::vector<Server>& input_servers) {
    servers = input_servers;
}

bool ResourceManager::try_allocate(const Job& job, int& out_server_id, std::vector<int>& out_gpus) {
    int required_per_gpu = 0;
    if (job.g > 0) {
        required_per_gpu = (job.v + job.g - 1) / job.g;
    }

    int best_server_idx = -1;
    std::vector<int> best_gpus;
    long long min_fragmentation = 9e18; 

    for (size_t i = 0; i < servers.size(); ++i) {
        auto& server = servers[i];

        if (server.free_cpu < job.c || server.free_mem < job.m) {
            continue; 
        }

        std::vector<int> available_gpus;
        if (job.g > 0) {
            for (int g_idx = 0; g_idx < server.G; ++g_idx) {
                if (server.gpu_free_mem[g_idx] >= required_per_gpu) {
                    available_gpus.push_back(g_idx);
                }
            }
            if (static_cast<int>(available_gpus.size()) < job.g) {
                continue; 
            }
            available_gpus.resize(job.g);
        }

        long long cpu_frag = server.free_cpu - job.c;
        if (cpu_frag < min_fragmentation) {
            min_fragmentation = cpu_frag;
            best_server_idx = i;
            best_gpus = available_gpus;
        }
    }

    if (best_server_idx != -1) {
        auto& target_server = servers[best_server_idx];
        target_server.free_cpu -= job.c;
        target_server.free_mem -= job.m;
        for (int g_idx : best_gpus) {
            target_server.gpu_free_mem[g_idx] -= required_per_gpu;
        }

        out_server_id = target_server.id;
        out_gpus = best_gpus;
        return true; 
    }

    return false; 
}

void ResourceManager::release_resources(const Job& job) {
    for (auto& server : servers) {
        if (server.id == job.allocated_server) {
            server.free_cpu += job.c;
            server.free_mem += job.m;
            if (job.g > 0) {
                int required_per_gpu = (job.v + job.g - 1) / job.g;
                for (int g_idx : job.allocated_gpus) {
                    server.gpu_free_mem[g_idx] += required_per_gpu;
                }
            }
            break;
        }
    }
}