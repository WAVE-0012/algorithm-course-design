#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

#include <vector>
#include "Server.h"
#include "Job.h"

class ResourceManager {
private:
    std::vector<Server> servers;

public:
    void init_servers(const std::vector<Server>& input_servers);
    bool try_allocate(const Job& job, int& out_server_id, std::vector<int>& out_gpus);
    void release_resources(const Job& job);
    const std::vector<Server>& get_servers() const { return servers; }
};

#endif