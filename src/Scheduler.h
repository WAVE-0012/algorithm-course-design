#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <vector>
#include "Job.h"
#include "ResourceManager.h"

class Scheduler {
private:
    ResourceManager* rm;

public:
    Scheduler(ResourceManager* resource_manager) : rm(resource_manager) {}
    void make_decisions(int current_time, std::vector<Job>& jobs);
};

#endif