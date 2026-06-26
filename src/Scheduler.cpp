#include "Scheduler.h"
#include <algorithm>

void Scheduler::make_decisions(int current_time, std::vector<Job>& jobs) {
    std::vector<Job*> ready_jobs;
    for (auto& job : jobs) {
        if (!job.is_scheduled && job.r <= current_time) {
            ready_jobs.push_back(&job);
        }
    }

    std::sort(ready_jobs.begin(), ready_jobs.end(), [](Job* a, Job* b) {
        if (a->w != b->w) return a->w > b->w;
        if (a->p != b->p) return a->p < b->p;
        return a->id < b->id;
    });

    for (auto* job : ready_jobs) {
        if (job->is_scheduled) continue;
        
        int allocated_server_id;
        std::vector<int> allocated_gpus;

        if (rm->try_allocate(*job, allocated_server_id, allocated_gpus)) {
            job->is_scheduled = true;
            job->start_time = current_time;
            job->allocated_server = allocated_server_id;
            job->allocated_gpus = allocated_gpus;
        }
    }
}