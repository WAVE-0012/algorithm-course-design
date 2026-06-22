#include<iostream>
#include<vector>
#include<algorithm>
#include"Server.h"
#include"Job.h"
#include"Scheduler.h"

using namespace std;

int M,N;
vector<Server> servers;
vector<Job> jobs;
int current_time=0;
int completed_count=0;

void load_input(){
    if(!(cin>>M>>N))return ;

    servers.resize(M);
    for(int i=0;i<M;++i){
        servers[i].id=i+1;
        cin>>servers[i].G>>servers[i].VG>>servers[i].C>>servers[i].R;
        servers[i].free_cpu=servers[i].C;
        servers[i].free_mem=servers[i].R;
        servers[i].gpu_free_mem.assign(servers[i].G,servers[i].VG);
    }
    jobs.resize(N);
    for(int i=0;i<N;++i){
        jobs[i].id=i+1;
        cin>>jobs[i].r>>jobs[i].p>>jobs[i].v>>jobs[i].c>>jobs[i].m>>jobs[i].w;
    }
}

void release_finished_jobs(){
    for(auto&job:jobs){
        if(job.is_scheduled&&(job.start_time+job.p==current_time)){
            Server&s=servers[job.allocated_server-1];
            s.free_cpu+=job.c;
            s.free_mem+=job.m;

            int required_per_gpu=(job.v+job.g-1)/job.g;
            for(int gpu_idx:job.allocated_gpus){
                s.gpu_free_mem[gpu_idx]+=required_per_gpu;
            }
            completed_count++;
        }
    }
}

void print_results(){
    sort(jobs.begin(),jobs.end(),[](const Job&a,const Job&b){
        return a.id<b.id;
    });
    
    for(const auto&job:jobs){
        cout<<job.id<<" "
        <<job.allocated_server<<" "
        <<job.start_time<<" "
        <<job.g<<" "
        <<job.start_time+job.p<<"\n";
    }
}

int main(){
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);

    load_input();
    Scheduler schedluer;

    while(completed_count<N){
        release_finished_jobs();

        if(completed_count==N) break;

        schedluer.make_decisions(current_time,servers,jobs);

        int next_time=2e9;

        for(const auto&job:jobs){
            if(!job.is_scheduled&&job.r>current_time){
                next_time=min(next_time,job.r);
                break;
            }
        }

        for(const auto& job:jobs){
            if(job.is_scheduled&&(job.start_time+job.p>current_time)){
                next_time=min(next_time,job.start_time+job.p);
            }
        }

        if(next_time!=2e9){
            current_time=next_time;
        }else{
            current_time++;
        }
    }
    print_results();
    return 0;
}