#ifndef SCHEDULED_H
#define SCHEDULED_H

#include<vector>
#include "Server.h"
#include "Job.h"

class Scheduler{
public:
    bool find_gpu_placement(Server&server,int g,int v,std::vector<int>&out_gpus);

    void make_decisions(int current_time,std::vector<Server>&servers,std::vector<Job>&jobs);
};

#endif
