#include"Scheduler.h"
#include<algorithm>
bool Scheduler::find_gpu_placement(Server& server,int g,int v,std::vector<int>&out_gpus){
    if(server.G<g) return false;

    int required_per_gpu=(v+g-1)/g;
    if(required_per_gpu>server.VG)return false;

    out_gpus.clear();
    for(int i=0;i<server.G;++i){
        if(server.gpu_free_mem[i]>=required_per_gpu){
            out_gpus.push_back(i);
            if(out_gpus.size()==g) return true;
        }
    }
    return false;
}

void Scheduler::make_decisions(int current_time,std::vector<Server>&servers,std::vector<Job>&jobs){
    for(auto&job:jobs){
        if(!job.is_scheduled&&job.r<=current_time){
            for(auto&server:servers){
                if(server.free_cpu>=job.c&&server.free_mem>=job.m){
                    std::vector<int> target_gpus;

                    if(find_gpu_placement(server,job.g,job.v,target_gpus)){

                        server.free_cpu-=job.c;
                        server.free_mem-=job.m;
                        int required_per_gpu=(job.v+job.g-1)/job.g;
                        for(int gpu_idx:target_gpus){
                            server.gpu_free_mem[gpu_idx]-=required_per_gpu;
                        }
                        job.is_scheduled=true;
                        job.start_time=current_time;
                        job.allocated_server=server.id;
                        job.allocated_gpus=target_gpus;
                        
                        break;
                    }
                }
            }
        }
    }
}