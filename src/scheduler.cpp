#include "scheduler.h"

#include <algorithm>
#include <stdexcept>
#include <queue>
#include <vector>
#include <cmath>
#include <limits>
#include <unordered_map>

using namespace std;

// ============================================================
// Priority 权重
// ============================================================

constexpr long long WEIGHT_FACTOR   = 120;
constexpr long long DURATION_FACTOR = 2;
constexpr long long GPU_FACTOR      = 25;
constexpr double AGING_SCALE        = 30.0;

// ============================================================

bool compareServerById(const ServerSpec &a, const ServerSpec &b) {
    return a.server_id < b.server_id;
}

bool FinishEvent::operator>(const FinishEvent &other) const {
    if (finish_time != other.finish_time) return finish_time > other.finish_time;
    if (server_id != other.server_id) return server_id > other.server_id;
    return job_id > other.job_id;
}

namespace {

// ============================================================
// Dynamic Priority Score
// ============================================================

long long dynamicPriorityScore(const Job& job, long long current_time) {
    long long waiting = current_time - job.release_time;
    if (waiting < 0) waiting = 0;

    long long score =
        job.weight * WEIGHT_FACTOR
        - job.duration * DURATION_FACTOR
        - job.min_gpu * GPU_FACTOR;

    score += static_cast<long long>(
        std::sqrt(static_cast<double>(waiting)) * AGING_SCALE
    );

    return score;
}

// ============================================================
// Fragment Penalty
// ============================================================

inline long long fragmentPenalty(int remain_gpu) {
    // 基于数据画像调整：GPU=1 占 38%，GPU=2 占 18%，GPU=3 占 10%
    // 惩罚那些留下"难以利用"碎片的情况
    switch(remain_gpu) {
        case 0: return 0;      // 完美利用
        case 1: return 0;      // 1 GPU 碎片，后续 38% 任务能用
        case 2: return 5;      // 2 GPU 碎片，后续 18% 任务能用，轻度惩罚
        case 3: return 15;     // 3 GPU 碎片，后续 10% 任务能用，中度惩罚
        case 4: return 5;      // 4 GPU 碎片，后续 7% 任务能用
        case 5: return 15;     // 5 GPU 碎片，后续 8% 任务能用
        case 6: return 8;      // 6 GPU 碎片，后续 9% 任务能用
        case 7: return 20;     // 7 GPU 碎片，几乎无法利用
        default: return 30;
    }
}

} // namespace

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
            if (a.release_time != b.release_time) return a.release_time < b.release_time;
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
// buildFeasibleMachines()
// ============================================================

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

        // GPU Packing：按 CPU 升序排序
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
// releaseFinishedJobs()
// ============================================================

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

// ============================================================
// tryStartOneJob() — CPU Best-Fit + Fragment Penalty
// ============================================================

GreedyScheduler::StartResult
GreedyScheduler::tryStartOneJob(
    const Job &job,
    long long current_time
) {

    const auto &entries =
        feasible_machines.at(job.job_id);

    int best_machine = -1;
    int best_gpu = -1;

    long long best_cost =
        numeric_limits<long long>::max();

    for (const auto &entry : entries) {

        int machine_index = entry.first;
        int gpu_used = entry.second;

        if (!machines[machine_index].canStart(job, gpu_used)) {
            continue;
        }

        const auto &machine = machines[machine_index];
        const auto &spec = machine.spec;

        long long remain_gpu_after =
            max(0LL,
                (long long)machine.getRemainingGpu() - gpu_used);

        long long remain_cpu_after =
            max(0LL,
                (long long)machine.getRemainingCpu() - job.cpu_cores);

        long long remain_mem_after =
            max(0LL,
                (long long)machine.getRemainingMemory() - job.memory);

        // =====================================================
        // CPU Best-Fit + Fragment Penalty
        // =====================================================
        long long cost =
            remain_cpu_after * 100
            + remain_gpu_after * 30
            + remain_mem_after * 10
            + fragmentPenalty((int)remain_gpu_after);

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
        machines[best_machine].startJob(
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

// ============================================================
// nextEventTime()
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

    if (next_job_index < static_cast<int>(jobs.size())) {
        next_time = min(
            next_time,
            static_cast<long long>(jobs[next_job_index].release_time)
        );
    }

    if (!running_heap.empty()) {
        next_time = min(
            next_time,
            running_heap.top().finish_time
        );
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
// schedule()
// ============================================================

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

    vector<Job> pending_vec;
    pending_vec.reserve(jobs.size());

    while ((int)records.size() < (int)jobs.size()) {

        releaseFinishedJobs(current_time, running_heap);

        while (next_job_index < (int)jobs.size() &&
               jobs[next_job_index].release_time <= current_time) {
            pending_vec.push_back(jobs[next_job_index]);
            ++next_job_index;
        }

        // =====================================================
        // 排序
        // =====================================================
        sort(pending_vec.begin(), pending_vec.end(),
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

        for (const auto &job : pending_vec) {

            auto started =
                tryStartOneJob(
                    job,
                    current_time
                );

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

            } else {
                remain.push_back(job);
            }
        }

        pending_vec.swap(remain);

        if ((int)records.size() == (int)jobs.size()) {
            break;
        }

        current_time =
            nextEventTime(
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

        ordered.push_back(
            records.at(job_id)
        );
    }

    return ordered;
}