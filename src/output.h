#ifndef GPU_SCHEDULING_OUTPUT_H
#define GPU_SCHEDULING_OUTPUT_H

#include <ostream>
#include <vector>

struct ScheduleRecord {
    int job_id;
    int server_id;
    long long start_time;
    int gpu_used;
    long long finish_time;
};

void writeScheduleRecords(std::ostream &output, const std::vector<ScheduleRecord> &records);

#endif