#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include "Server.h"
#include "Job.h"
#include "ResourceManager.h"
#include "Scheduler.h"
using namespace std;

int M, N;
vector<Job> jobs;
ResourceManager resource_manager;
int current_time = 0;
int completed_count = 0;

void load_input() {
    if (!(cin >> M >> N)) return;

    vector<Server> servers;
    servers.resize(M);
    for (int i = 0; i < M; ++i) {
        servers[i].id = i + 1;
        cin >> servers[i].G >> servers[i].VG >> servers[i].C >> servers[i].R;
        servers[i].free_cpu = servers[i].C;
        servers[i].free_mem = servers[i].R;
        servers[i].gpu_free_mem.assign(servers[i].G, servers[i].VG);
    }
    resource_manager.init_servers(servers);

    jobs.resize(N);
    for (int i = 0; i < N; ++i) {
        jobs[i].id = i + 1;
        cin >> jobs[i].r >> jobs[i].p >> jobs[i].g >> jobs[i].v 
            >> jobs[i].c >> jobs[i].m >> jobs[i].w;
        jobs[i].is_scheduled = false;
        jobs[i].is_completed = false;
    }
}

void adapt_job_requirements() {
    const auto& servers = resource_manager.get_servers();
    if (servers.empty()) return;

    for (auto& job : jobs) {
        bool can_fit_anywhere = false;
        int req_per_gpu = (job.g > 0) ? (job.v + job.g - 1) / job.g : 0;

        for (const auto& server : servers) {
            if (server.C >= job.c && server.R >= job.m && server.G >= job.g && server.VG >= req_per_gpu) {
                can_fit_anywhere = true;
                break;
            }
        }

        if (can_fit_anywhere) continue;

        double min_scale_factor = 2e9;
        size_t anchor_idx = 0;

        for (size_t i = 0; i < servers.size(); ++i) {
            double f_c = (job.c > servers[i].C) ? (double)job.c / servers[i].C : 1.0;
            double f_m = (job.m > servers[i].R) ? (double)job.m / servers[i].R : 1.0;
            double f_g = (job.g > servers[i].G) ? (double)job.g / servers[i].G : 1.0;
            double f_v = 1.0;
            if (job.g > 0 && req_per_gpu > servers[i].VG) {
                f_v = (double)req_per_gpu / servers[i].VG;
            }
            double max_f = max({f_c, f_m, f_g, f_v});
            if (max_f < min_scale_factor) {
                min_scale_factor = max_f;
                anchor_idx = i;
            }
        }

        const auto& anchor = servers[anchor_idx];
        if (job.c > anchor.C) job.c = anchor.C;
        if (job.m > anchor.R) job.m = anchor.R;
        if (job.g > anchor.G) job.g = anchor.G;
        if (job.g > 0) {
            int current_req = (job.v + job.g - 1) / job.g;
            if (current_req > anchor.VG) {
                job.v = anchor.VG * job.g;
            }
        }
        job.p = ceil(job.p * min_scale_factor);
    }
}

void release_finished_jobs() {
    for (auto& job : jobs) {
        if (job.is_scheduled && !job.is_completed && (job.start_time + job.p <= current_time)) {
            resource_manager.release_resources(job);
            job.is_completed = true;
            completed_count++;
        }
    }
}

void print_results() {
    sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) {
        return a.id < b.id;
    });

    for (const auto& job : jobs) {
        cout << job.id << " "
             << job.allocated_server << " "
             << job.start_time << " "
             << job.g << " "
             << job.start_time + job.p << "\n";
    }
}

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL),cout.tie(NULL);

    load_input();
    
    adapt_job_requirements();
    Scheduler scheduler(&resource_manager);

    while (completed_count < N) {
        release_finished_jobs();
        if (completed_count == N) break;

        scheduler.make_decisions(current_time, jobs);

        int next_time = 2e9;
        bool has_next_event = false;

        for (const auto& job : jobs) {
            if (!job.is_scheduled && job.r > current_time) {
                next_time = min(next_time, job.r);
                has_next_event = true;
            }
        }

        for (const auto& job : jobs) {
            if (job.is_scheduled && !job.is_completed) {
                next_time = min(next_time, job.start_time + job.p);
                has_next_event = true;
            }
        }

        if (has_next_event && next_time != 2e9) {
            current_time = next_time;
        } else {
            current_time++; 
        }
    }

    print_results();

    return 0;
}