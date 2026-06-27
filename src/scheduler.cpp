#include "scheduler.h"

#include <algorithm>
#include <queue>
#include <vector>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <stdexcept>

using namespace std;

// ============================================================
// 基础常量
// ============================================================

constexpr long long WEIGHT_FACTOR   = 120;
constexpr long long DURATION_FACTOR = 2;
constexpr long long GPU_FACTOR      = 25;
constexpr double AGING_SCALE        = 30.0;

// ============================================================
// compareServerById
// ============================================================

bool compareServerById(const ServerSpec &a, const ServerSpec &b) {
    return a.server_id < b.server_id;
}

// ============================================================
// FinishEvent
// ============================================================

bool FinishEvent::operator>(const FinishEvent &other) const {
    if (finish_time != other.finish_time) return finish_time > other.finish_time;
    if (server_id != other.server_id) return server_id > other.server_id;
    return job_id > other.job_id;
}

// ============================================================
// 构造函数
// ============================================================

GreedyScheduler::GreedyScheduler(
    vector<ServerSpec> input_servers,
    vector<Job> input_jobs
)
    : servers(move(input_servers)),
      jobs(move(input_jobs)) {

    sort(servers.begin(), servers.end(), compareServerById);
    sort(jobs.begin(), jobs.end(),
        [](const Job &a, const Job &b) {
            if (a.release_time != b.release_time)
                return a.release_time < b.release_time;
            return a.job_id < b.job_id;
        });

    for (const auto& server : servers) {
        machines.emplace_back(server);
    }

    for (size_t i = 0; i < machines.size(); ++i) {
        machine_index_by_id[machines[i].spec.server_id] = static_cast<int>(i);
    }

    buildFeasibleMachines();
}

// ============================================================
// buildFeasibleMachines
// ============================================================

void GreedyScheduler::buildFeasibleMachines() {

    for (const auto &job : jobs) {

        vector<pair<int,int>> entries;

        for (int i = 0; i < (int)machines.size(); i++) {

            int gpu_used = machines[i].requiredGpuCount(job);

            if (machines[i].canEverRun(job, gpu_used)) {
                entries.push_back({i, gpu_used});
            }
        }

        if (entries.empty()) {
            throw runtime_error("A job cannot run on any server.");
        }

        sort(entries.begin(), entries.end(),
            [this](const pair<int,int> &a, const pair<int,int> &b) {
                const auto &A = machines[a.first].spec;
                const auto &B = machines[b.first].spec;

                if (A.cpu_cores != B.cpu_cores) return A.cpu_cores < B.cpu_cores;
                if (A.memory != B.memory) return A.memory < B.memory;
                return a.first < b.first;
            });

        feasible_machines[job.job_id] = move(entries);
    }
}

// ============================================================
// releaseFinishedJobs
// ============================================================

void GreedyScheduler::releaseFinishedJobs(
    long long current_time,
    priority_queue<
        FinishEvent,
        vector<FinishEvent>,
        greater<FinishEvent>
    >& running_heap
) {
    while (!running_heap.empty() &&
           running_heap.top().finish_time <= current_time) {

        FinishEvent event = running_heap.top();
        running_heap.pop();

        int idx = machine_index_by_id.at(event.server_id);
        machines[idx].releaseJob(event.running_job);
    }
}

// ============================================================
// nextEventTime
// ============================================================

long long GreedyScheduler::nextEventTime(
    long long current_time,
    int next_job_index,
    const priority_queue<
        FinishEvent,
        vector<FinishEvent>,
        greater<FinishEvent>
    >& running_heap
) const {

    long long next_time = numeric_limits<long long>::max();

    if (next_job_index < (int)jobs.size()) {
        next_time = min(next_time, (long long)jobs[next_job_index].release_time);
    }

    if (!running_heap.empty()) {
        next_time = min(next_time, running_heap.top().finish_time);
    }

    if (next_time == numeric_limits<long long>::max()) {
        throw runtime_error("No future event exists.");
    }

    if (next_time <= current_time) {
        next_time = current_time + 1;
    }

    return next_time;
}

// ============================================================
// dynamicPriorityScore（稳定版核心）
// ============================================================

long long dynamicPriorityScore(const Job& job, long long current_time) {

    long long waiting = current_time - job.release_time;
    if (waiting < 0) waiting = 0;

    long long score =
        job.weight * WEIGHT_FACTOR
        - job.duration * DURATION_FACTOR
        - job.min_gpu * GPU_FACTOR;

    // ⭐ sqrt aging 防止爆炸
    score += static_cast<long long>(sqrt(static_cast<double>(waiting)) * AGING_SCALE);

    return score;
}

// ============================================================
// tryStartOneJob（CPU Best-Fit）
// ============================================================

GreedyScheduler::StartResult
GreedyScheduler::tryStartOneJob(
    const Job &job,
    long long current_time
) {

    const auto &entries = feasible_machines.at(job.job_id);

    int best_machine = -1;
    long long best_cost = numeric_limits<long long>::max();
    int best_gpu = -1;

    for (const auto &entry : entries) {

        int idx = entry.first;
        int gpu_used = entry.second;

        if (!machines[idx].canStart(job, gpu_used)) continue;

        const auto &m = machines[idx];
        const auto &spec = m.spec;

        long long used_cpu = spec.cpu_cores - m.getRemainingCpu();
        long long used_gpu = spec.gpu_count - m.getRemainingGpu();
        long long used_mem = spec.memory - m.getRemainingMemory();

        long long rem_cpu = spec.cpu_cores - used_cpu;
        long long rem_gpu = spec.gpu_count - used_gpu;
        long long rem_mem = spec.memory - used_mem;

        // ⭐ CPU Best-Fit（稳定关键）
        long long cost =
            rem_cpu * 100 +
            rem_gpu * 30 +
            rem_mem * 10;

        if (cost < best_cost) {
            best_cost = cost;
            best_machine = idx;
            best_gpu = gpu_used;
        }
    }

    if (best_machine == -1) {
        return StartResult{};
    }

    auto result =
        machines[best_machine].startJob(job, current_time, best_gpu);

    return StartResult{
        true,
        result.first,
        result.second
    };
}

// ============================================================
// schedule()
// ============================================================

vector<ScheduleRecord> GreedyScheduler::schedule() {

    if (jobs.empty()) return {};

    long long current_time = jobs.front().release_time;
    int next_job_index = 0;

    unordered_map<int, ScheduleRecord> records;

    priority_queue<
        FinishEvent,
        vector<FinishEvent>,
        greater<FinishEvent>
    > running_heap;

    vector<Job> pending;
    pending.reserve(jobs.size());

    while ((int)records.size() < (int)jobs.size()) {

        releaseFinishedJobs(current_time, running_heap);

        while (next_job_index < (int)jobs.size() &&
               jobs[next_job_index].release_time <= current_time) {
            pending.push_back(jobs[next_job_index++]);
        }

        // =====================================================
        // 排序（稳定核心）
        // =====================================================
        sort(pending.begin(), pending.end(),
            [this, current_time](const Job &a, const Job &b) {

                long long sa = dynamicPriorityScore(a, current_time);
                long long sb = dynamicPriorityScore(b, current_time);

                if (sa != sb) return sa > sb;

                if (a.duration != b.duration) return a.duration < b.duration;
                if (a.min_gpu != b.min_gpu) return a.min_gpu < b.min_gpu;
                return a.job_id < b.job_id;
            });

        // =====================================================
        // 调度
        // =====================================================
        vector<Job> remain;

        for (const auto &job : pending) {

            auto res = tryStartOneJob(job, current_time);

            if (res.has_value) {

                records[job.job_id] = res.record;

                running_heap.push(FinishEvent{
                    res.running_job.finish_time,
                    res.running_job.server_id,
                    res.running_job.job_id,
                    res.running_job
                });

            } else {
                remain.push_back(job);
            }
        }

        pending.swap(remain);

        if ((int)records.size() == (int)jobs.size()) break;

        current_time = nextEventTime(current_time, next_job_index, running_heap);
    }

    vector<ScheduleRecord> out;
    out.reserve(records.size());

    for (int i = 1; i <= (int)jobs.size(); ++i) {
        out.push_back(records.at(i));
    }

    return out;
}