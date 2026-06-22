#include "output.h"
#include <algorithm>

bool compareByJobId(const ScheduleRecord &a, const ScheduleRecord &b) {
    return a.job_id < b.job_id;
}

void writeScheduleRecords(std::ostream &output, const std::vector<ScheduleRecord> &records) {
    std::vector<ScheduleRecord> sorted = records;
    std::sort(sorted.begin(), sorted.end(), compareByJobId);

    for (const auto &r : sorted) {
        output << r.job_id << " "
               << r.server_id << " "
               << r.start_time << " "
               << r.gpu_used << " "
               << r.finish_time << "\n";
    }
}