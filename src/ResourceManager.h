#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

#include <vector>
#include "Server.h"
#include "Job.h"

class ResourceManager {
private:
    std::vector<Server> servers; // 所有的服务器资源收归我有

    // 高性能优化：根据服务器的特性建立索引（桶）
    // 例如：按照单卡显存(VG)分类存放服务器的指针，避免盲目遍历
    // std::map<int, std::vector<Server*>> vg_index; 

public:
    // 初始化：读入并管理所有服务器
    void init_servers(const std::vector<Server>& input_servers);

    // 核心接口 1：尝试为任务寻找最合适的服务器和 GPU 卡
    // 如果找到，直接在内部扣除资源，并返回 true 以及分配结果
    bool try_allocate(const Job& job, int& out_server_id, std::vector<int>& out_gpus);

    // 核心接口 2：任务结束时，释放其占用的资源
    void release_resources(const Job& job);

    // 辅助接口：获取当前的服务器列表（只读，加 const 确保安全和性能）
    const std::vector<Server>& get_servers() const { return servers; }
};

#endif