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

constexpr long long WEIGHT_FACTOR   = 100000LL;
constexpr long long DURATION_FACTOR = 50LL;
constexpr long long GPU_FACTOR      = 20LL;
constexpr long long CPU_FACTOR      = 1LL;
constexpr long long MEM_FACTOR      = 1LL;

// Aging
constexpr double AGING_SCALE = 2000.0;

// Easy Start（整数计算，避免截断）
constexpr long long EASY_START_SCALE = 500LL;

// Dynamic Best-Fit 权重
constexpr double REMAIN_GPU_WEIGHT      = 2.0;
constexpr double REMAIN_CPU_WEIGHT      = 1.0;
constexpr double REMAIN_MEM_WEIGHT      = 0.5;
constexpr double FRAGMENT_PENALTY_WEIGHT = 3.0;

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
    long long score =
        WEIGHT_FACTOR   * job.weight
        - DURATION_FACTOR * job.duration
        - GPU_FACTOR      * job.min_gpu
        - CPU_FACTOR      * job.cpu_cores
        - MEM_FACTOR      * (job.memory / 100);

    long long waiting = current_time - job.release_time;
    if (waiting < 0) waiting = 0;

    score += static_cast<long long>(
        std::sqrt(static_cast<double>(waiting)) * AGING_SCALE
    );

    score += EASY_START_SCALE / (job.min_gpu + 1);

    return score;
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
        // 动态统计等待队列 GPU 75% 分位数
        // =====================================================
        if (!pending_vec.empty()) {

            vector<int> gpu_demands;
            gpu_demands.reserve(pending_vec.size());

            for (const auto &job : pending_vec) {
                gpu_demands.push_back(job.min_gpu);
            }

            size_t idx = static_cast<size_t>(gpu_demands.size() * 3 / 4);
            if (idx >= gpu_demands.size()) idx = gpu_demands.size() - 1;

            nth_element(
                gpu_demands.begin(),
                gpu_demands.begin() + idx,
                gpu_demands.end()
            );

            cached_dynamic_threshold_gpu = gpu_demands[idx];
        }
        else {
            cached_dynamic_threshold_gpu = 1;
        }

        // =====================================================
        // 回填调度 + Fit-aware Priority + 同优先级小GPU优先
        // =====================================================
        bool progress = true;

        while (progress) {

            progress = false;

            // 每轮重建缓存
            vector<char> can_start(jobs.size() + 1, 0);
            for (const auto &job : pending_vec) {
                const auto &entries = feasible_machines.at(job.job_id);
                for (const auto &entry : entries) {
                    if (machines[entry.first].canStart(job, entry.second)) {
                        can_start[job.job_id] = 1;
                        break;
                    }
                }
            }

            sort(pending_vec.begin(), pending_vec.end(),
                [this, current_time, &can_start](const Job &a, const Job &b) {
                    bool a_can = can_start[a.job_id] == 1;
                    bool b_can = can_start[b.job_id] == 1;

                    if (a_can && !b_can) return true;
                    if (!a_can && b_can) return false;

                    long long sa = dynamicPriorityScore(a, current_time);
                    long long sb = dynamicPriorityScore(b, current_time);

                    if (sa != sb) return sa > sb;

                    // ===== 同优先级下：小GPU优先 =====
                    if (a.min_gpu != b.min_gpu)
                        return a.min_gpu < b.min_gpu;

                    if (a.duration != b.duration)
                        return a.duration < b.duration;

                    return a.job_id < b.job_id;
                }
            );

            vector<Job> remain;
            remain.reserve(pending_vec.size());

            for (const auto &job : pending_vec) {

                auto started =
                    tryStartOneJob(
                        job,
                        current_time
                    );

                if (started.has_value) {

                    records[job.job_id] =
                        started.record;

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

            pending_vec.swap(remain);
        }

        if ((int)records.size() ==
            (int)jobs.size()) {
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

        // =====================================================
        // GPU Packing：按浪费最少排序
        // =====================================================
        sort(entries.begin(), entries.end(),
            [this](const pair<int,int> &a, const pair<int,int> &b) {
                const auto &specA = machines[a.first].spec;
                const auto &specB = machines[b.first].spec;

                int wasteA = specA.gpu_count - a.second;
                int wasteB = specB.gpu_count - b.second;

                if (wasteA != wasteB)
                    return wasteA < wasteB;

                if (specA.cpu_cores != specB.cpu_cores)
                    return specA.cpu_cores < specB.cpu_cores;

                if (specA.memory != specB.memory)
                    return specA.memory < specB.memory;

                return a.first < b.first;
            }
        );

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
// tryStartOneJob()
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

    double best_cost =
        numeric_limits<double>::max();

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

        double gpu_ratio =
            static_cast<double>(remain_gpu_after) /
            max(1, spec.gpu_count);

        double cpu_ratio =
            static_cast<double>(remain_cpu_after) /
            max(1, spec.cpu_cores);

        double mem_ratio =
            static_cast<double>(remain_mem_after) /
            max(1, spec.memory);

        // =====================================================
        // 碎片惩罚（连续值）
        // =====================================================
        double fragment_penalty = 0.0;

        if (remain_gpu_after > 0 &&
            remain_gpu_after < cached_dynamic_threshold_gpu) {

            fragment_penalty =
                static_cast<double>(
                    cached_dynamic_threshold_gpu -
                    remain_gpu_after
                ) /
                max(1, cached_dynamic_threshold_gpu);
        }

        double cost =
              REMAIN_GPU_WEIGHT * gpu_ratio
            + REMAIN_CPU_WEIGHT * cpu_ratio
            + REMAIN_MEM_WEIGHT * mem_ratio
            + FRAGMENT_PENALTY_WEIGHT * fragment_penalty;

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