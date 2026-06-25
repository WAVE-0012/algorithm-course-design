#include "scheduler.h"

#include <algorithm>
#include <stdexcept>
#include <queue>

using namespace std;

bool compareServerById(const ServerSpec &a, const ServerSpec &b) {
    return a.server_id < b.server_id;
}

bool compareJobByRelease(const Job &a, const Job &b) {
    if (a.release_time != b.release_time) return a.release_time < b.release_time;
    return a.job_id < b.job_id;
}

bool FinishEvent::operator>(const FinishEvent &other) const {
    if (finish_time != other.finish_time) return finish_time > other.finish_time;
    if (server_id != other.server_id) return server_id > other.server_id;
    return job_id > other.job_id;
}

namespace {

long long priorityScore(const Job& job) {
    return
        100000LL * job.weight
        - 50LL * job.duration
        - 20LL * job.min_gpu
        - job.cpu_cores
        - job.memory / 100;
}

struct JobCmp {
    bool operator()(const Job& a, const Job& b) const {
        long long sa = priorityScore(a);
        long long sb = priorityScore(b);

        if (sa != sb)
            return sa < sb;

        if (a.duration != b.duration)
            return a.duration > b.duration;

        return a.job_id > b.job_id;
    }
};

}

GreedyScheduler::GreedyScheduler(
    vector<ServerSpec> input_servers,
    vector<Job> input_jobs
)
    : servers(move(input_servers)),
      jobs(move(input_jobs)) {

    sort(servers.begin(), servers.end(), compareServerById);
    sort(jobs.begin(), jobs.end(), compareJobByRelease);

    for (const auto& server : servers) {
        machines.emplace_back(server);
    }

    for (int i = 0; i < (int)machines.size(); ++i) {
        machine_index_by_id[machines[i].spec.server_id] = i;
    }

    buildFeasibleMachines();
}

vector<ScheduleRecord> GreedyScheduler::schedule() {

    if (jobs.empty()) {
        return {};
    }

    long long current_time = jobs.front().release_time;

    int next_job_index = 0;

    unordered_map<int, ScheduleRecord> records;

    priority_queue<
        FinishEvent,
        vector<FinishEvent>,
        greater<FinishEvent>
    > running_heap;

    priority_queue<Job, vector<Job>, JobCmp> pending_jobs;

    while ((int)records.size() < (int)jobs.size()) {

        releaseFinishedJobs(current_time, running_heap);

        while (next_job_index < (int)jobs.size()
               && jobs[next_job_index].release_time <= current_time) {

            pending_jobs.push(jobs[next_job_index]);
            ++next_job_index;
        }

        bool progress = true;

        while (progress) {

            progress = false;

            vector<Job> remain;

            while (!pending_jobs.empty()) {

                Job job = pending_jobs.top();
                pending_jobs.pop();

                auto started = tryStartOneJob(job, current_time);

                if (started.has_value) {

                    records[job.job_id] = started.record;

                    running_heap.push(
                        FinishEvent{
                            started.running_job.finish_time,
                            started.running_job.server_id,
                            started.running_job.job_id,
                            started.running_job
                        }
                    );

                    progress = true;
                }
                else {
                    remain.push_back(job);
                }
            }

            for (auto &j : remain) {
                pending_jobs.push(j);
            }
        }

        if ((int)records.size() == (int)jobs.size()) {
            break;
        }

        current_time = nextEventTime(
            current_time,
            next_job_index,
            running_heap
        );
    }

    vector<ScheduleRecord> ordered;

    ordered.reserve(records.size());

    for (int job_id = 1;
         job_id <= (int)jobs.size();
         ++job_id) {

        ordered.push_back(records.at(job_id));
    }

    return ordered;
}

void GreedyScheduler::buildFeasibleMachines() {

    for (const auto &job : jobs) {

        vector<pair<int,int>> entries;

        for (int index = 0;
             index < (int)machines.size();
             ++index) {

            int gpu_used =
                machines[index].requiredGpuCount(job);

            if (machines[index].canEverRun(job, gpu_used)) {
                entries.push_back({index, gpu_used});
            }
        }

        if (entries.empty()) {
            throw runtime_error(
                "A job cannot run on any server."
            );
        }

        feasible_machines[job.job_id] = move(entries);
    }
}

void GreedyScheduler::releaseFinishedJobs(
    long long current_time,
    priority_queue<
        FinishEvent,
        vector<FinishEvent>,
        greater<FinishEvent>
    >& running_heap
) {
    while (!running_heap.empty()
           && running_heap.top().finish_time <= current_time) {

        FinishEvent event = running_heap.top();
        running_heap.pop();

        int machine_index =
            machine_index_by_id.at(event.server_id);

        machines[machine_index]
            .releaseJob(event.running_job);
    }
}

void GreedyScheduler::tryStartPendingJobs(
    queue<Job>&,
    long long,
    unordered_map<int, ScheduleRecord>&,
    priority_queue<
        FinishEvent,
        vector<FinishEvent>,
        greater<FinishEvent>
    >&
) {
}

GreedyScheduler::StartResult
GreedyScheduler::tryStartOneJob(
    const Job &job,
    long long current_time
) {

    const auto& entries =
        feasible_machines.at(job.job_id);

    int best_machine = -1;
    int best_gpu = -1;

    long long best_cost =
        (1LL << 60);

    for (const auto& entry : entries) {

        int machine_index = entry.first;
        int gpu_used = entry.second;

        if (!machines[machine_index]
                 .canStart(job, gpu_used))
            continue;

        const auto& spec =
            machines[machine_index].spec;

        long long waste_gpu =
            spec.gpu_count - gpu_used;

        long long waste_cpu =
            spec.cpu_cores - job.cpu_cores;

        long long waste_mem =
            spec.memory - job.memory;

        long long cost =
            waste_gpu * 1000000LL
            + waste_cpu * 1000LL
            + waste_mem;

        if (cost < best_cost) {
            best_cost = cost;
            best_machine = machine_index;
            best_gpu = gpu_used;
        }
    }

    if (best_machine == -1) {
        return StartResult{};
    }

    auto result =
        machines[best_machine]
            .startJob(
                job,
                current_time,
                best_gpu
            );

    return StartResult{
        true,
        result.first,
        result.second
    };
}

long long GreedyScheduler::nextEventTime(
    long long current_time,
    int next_job_index,
    const priority_queue<
        FinishEvent,
        vector<FinishEvent>,
        greater<FinishEvent>
    >& running_heap
) const {

    vector<long long> candidates;

    if (next_job_index < (int)jobs.size()) {
        candidates.push_back(
            jobs[next_job_index].release_time
        );
    }

    if (!running_heap.empty()) {
        candidates.push_back(
            running_heap.top().finish_time
        );
    }

    long long next_time = -1;

    for (long long t : candidates) {

        if (t <= current_time)
            continue;

        if (next_time == -1 || t < next_time) {
            next_time = t;
        }
    }

    if (next_time == -1) {
        throw runtime_error(
            "No future event exists."
        );
    }

    return next_time;
}