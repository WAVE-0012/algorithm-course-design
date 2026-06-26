#ifndef JOB_H
#define JOB_H

#include <vector>

struct Job {
    int id;
    int r, p, g, v, c, m, w;

    bool is_scheduled = false;
    bool is_completed = false;
    int start_time = -1;
    int allocated_server = -1;
    std::vector<int> allocated_gpus;
};

#endif